add_library(components OBJECT
    # Busses
    busses/MainbusComponent.cc

    # CPU
    cpu/CPUComponent.cc
    cpu/CPUDyntransComponent.cc
    cpu/M88K_CPUComponent.cc
    cpu/MIPS_CPUComponent.cc

    # Machines
    machines/MachineComponent.cc
    machines/MVME187Machine.cc
    machines/SGI_IP30_Machine.cc
    machines/SGI_IP32_Machine.cc
    machines/TestM88KMachine.cc
    machines/TestMIPSMachine.cc

    # Memory
    memory/CacheComponent.cc
    memory/MemoryMappedComponent.cc
    memory/RAMComponent.cc

    # Special
    special/DummyComponent.cc
    special/RootComponent.cc
)
