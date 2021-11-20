#include "misc.h"
#include "mem_passthrough.h"
#if 0
#include <sstream>
#include <boost/asio.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#endif

extern int verbose;

// Implementation based on
// https://gist.github.com/yoggy/3323808
#if 0
class SerialMemoryHolePassthrough : public IMemoryHolePassthrough {
public:
    SerialMemoryHolePassthrough() {
    }
    ~SerialMemoryHolePassthrough() {
    }
    bool start(const char *config) {
        boost::system::error_code ec;
        if (serial_port) {
            return false;
        }
        boost::property_tree::ptree config_data;
        std::istringstream config_str(config);
        read_json(config_str, config_data);
        serial_port.reset(new boost::asio::serial_port(io_service));
        std::string port_file = config_data.get<std::string>("port");
        if (port_file.empty()) {
            return false;
        }
        int port_baud = config_data.get<int>("baud", 9600);
        int char_size = config_data.get<int>("bits", 8);
        int stop_bits = config_data.get<int>("stop", 1);
        std::string parity_str = config_data.get<std::string>("parity", "none");
        std::string flow_str = config_data.get<std::string>("flow", "none");
        struct {
            const char *par_name;
            boost::asio::serial_port_base::parity par_val;
        } parity_map[] = {
            { "even", boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::even) },
            { "odd", boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::odd) },
            { NULL, boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none) }
        };
        boost::asio::serial_port_base::parity parity;
        int i;
        for (i = 0; parity_map[i].par_name && parity_map[i].par_name != parity_str; i++) { }
        parity = parity_map[i].par_val;
        boost::asio::serial_port_base::flow_control flow_control;
        struct {
            const char *flow_name;
            boost::asio::serial_port_base::flow_control flow_val;
        } flow_control_map[] = {
            { "hardware", boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::hardware) },
            { "software", boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::software) },
            { NULL, boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none) }
        };
        for (i = 0; flow_control_map[i].flow_name && flow_control_map[i].flow_name != flow_str; i++) { }
        flow_control = flow_control_map[i].flow_val;
        debug
            ("[ MP Open port=%s baud=%d bits=%d parity=%s flow=%s ]\n",
             port_file.c_str(),
             port_baud,
             char_size,
             parity_str.c_str(),
             flow_str.c_str());
        serial_port->open(port_file, ec);
        if (ec) {
            serial_port.reset();
            return false;
        }
        serial_port->set_option(boost::asio::serial_port_base::baud_rate(port_baud));
        serial_port->set_option(boost::asio::serial_port_base::character_size(char_size));
        serial_port->set_option(boost::asio::serial_port_base::stop_bits(stop_bits == 2 ? boost::asio::serial_port_base::stop_bits::two : boost::asio::serial_port_base::stop_bits::one));
        serial_port->set_option(boost::asio::serial_port_base::parity(parity));
        serial_port->set_option(boost::asio::serial_port_base::flow_control(flow_control));

        io_thread = boost::thread(boost::bind(&boost::asio::io_service::run, &io_service));

        async_read_perform();
        return true;
    }
    void write(uint64_t paddr, const unsigned char *data, size_t len) {
        if (!serial_port || !len || paddr < 0x80000000) {
            return;
        }
        size_t i;
        std::ostringstream oss;
        const char *len_char = "!wV!W";
        oss << len_char[len] << std::setfill('0') << std::hex << std::setw(8) << (int)paddr;
        for (i = 0; i < len; i++) {
            oss << std::hex << std::setw(2) << (int)(data[i] & 0xff);
        }
        std::string cmd_str = oss.str();
        boost::asio::async_write
            (*serial_port, 
             boost::asio::buffer(cmd_str.c_str(), cmd_str.size()),
             boost::bind
             (&SerialMemoryHolePassthrough::on_sent,
              this,
              boost::asio::placeholders::error,
              boost::asio::placeholders::bytes_transferred));
        read_line();
    }
    void read(uint64_t paddr, unsigned char *data, size_t len) {
        if (!serial_port || !len || paddr < 0x80000000) {
            return;
        }
        size_t i;
        std::ostringstream oss;
        const char *len_char = "!rB!R";
        oss << len_char[len] << std::setfill('0') << std::hex << std::setw(8) << (unsigned int)paddr;
        std::string cmd_str = oss.str();
        boost::asio::async_write
            (*serial_port, 
             boost::asio::buffer(cmd_str.c_str(), cmd_str.size()),
             boost::bind
             (&SerialMemoryHolePassthrough::on_sent,
              this,
              boost::asio::placeholders::error,
              boost::asio::placeholders::bytes_transferred));
        std::string res = read_line();
        std::istringstream iss(res);
        iss.width(2);
        for (i = 0; i < len; i++) {
            std::string bytebuf;
            iss >> bytebuf;
            std::istringstream byteread(bytebuf);
            int recv;
            byteread >> std::hex >> recv;
            data[i] = recv;
        }
    }

private:
    bool read_buf_not_empty() const { return !read_buf.empty(); }
    std::string read_line() {
        std::string line;
        bool got_eol = false;
        while (!got_eol) {
            boost::unique_lock<boost::mutex> _l(mutex);
            event.wait(_l, boost::bind(&SerialMemoryHolePassthrough::read_buf_not_empty, this));
            size_t found_eol = read_buf.find('\n');
            if (found_eol != std::string::npos) {
                got_eol = true;
                line = read_buf.substr(0, found_eol);
                read_buf = read_buf.substr(found_eol+1);
                while (found_eol && isspace(read_buf[found_eol-1])) {
                    found_eol--;
                }
                size_t prompt = 0;
                while (prompt < line.size() && line[prompt] == '>') {
                    prompt++;
                }
                line = line.substr(prompt,found_eol);
            }
        }
        return line;
    }

    void on_sent(const boost::system::error_code &ec, size_t bytes) {
    }

    void on_receive(const boost::system::error_code &ec, size_t bytes) {
        boost::unique_lock<boost::mutex> _l(mutex);
        read_buf += std::string(read_buf_raw, bytes);
        event.notify_all();
        async_read_perform();
    }

    void async_read_perform() {
        if (serial_port.get() == NULL || !serial_port->is_open()) {
            return;
        }
        serial_port->async_read_some
            (boost::asio::buffer
             (read_buf_raw, sizeof(read_buf_raw)),
             boost::bind
             (&SerialMemoryHolePassthrough::on_receive,
              this,
              boost::asio::placeholders::error,
              boost::asio::placeholders::bytes_transferred));
    }

    char read_buf_raw[256];
    std::string read_buf;

    boost::asio::io_service io_service;
    boost::shared_ptr<boost::asio::serial_port> serial_port;
    boost::thread io_thread;
    boost::condition_variable event;
    boost::mutex mutex;
};
#endif

IMemoryHolePassthrough *create_memory_hole_handler(const char *config) {
  //SerialMemoryHolePassthrough *hole = new SerialMemoryHolePassthrough();
  //hole->start(config);
  //return hole;
  return nullptr;
}
