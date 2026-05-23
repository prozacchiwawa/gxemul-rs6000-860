
add_executable(generate_components generate_components.cc)
add_executable(generate_commands generate_commands.cc)

add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/components_h.h ${CMAKE_BINARY_DIR}/components.h
                   COMMAND $<TARGET_FILE:generate_components> ${CMAKE_SOURCE_DIR}/src/include/components/ ${CMAKE_BINARY_DIR}/components_h.h ${CMAKE_BINARY_DIR}/components.h
                   DEPENDS generate_components
                   VERBATIM)

add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/commands_h.h ${CMAKE_BINARY_DIR}/commands.h
                   COMMAND $<TARGET_FILE:generate_commands> ${CMAKE_SOURCE_DIR}/src/main/commands/ ${CMAKE_BINARY_DIR}/commands_h.h ${CMAKE_BINARY_DIR}/commands.h
                   DEPENDS generate_commands
                   VERBATIM)

add_library(main
    OBJECT
    ComponentFactory.cc
    fileloaders/FileLoader_ELF.cc
    fileloaders/FileLoader_aout.cc
    fileloaders/FileLoader_raw.cc
    MemoryPassthrough.cc
    StateVariable.cc
    Component.cc
    Command.cc
    StringHelper.cc
    EscapedString.cc
    FileLoader.cc
    CommandInterpreter.cc
    Checksum.cc
    SymbolRegistry.cc
    commands/MoveComponentCommand.cc
    commands/PauseCommand.cc
    commands/CopyComponentCommand.cc
    commands/VersionCommand.cc
    commands/RemoveComponentCommand.cc
    commands/ResetCommand.cc
    commands/LoadCommand.cc
    commands/StepCommand.cc
    commands/HelpCommand.cc
    commands/ListComponentsCommand.cc
    commands/ContinueCommand.cc
    commands/ContinueBackwardsCommand.cc
    commands/AddComponentCommand.cc
    commands/SaveCommand.cc
    commands/CloseCommand.cc
    commands/StatusCommand.cc
    commands/BackwardStepCommand.cc
    commands/QuitCommand.cc
    ${CMAKE_BINARY_DIR}/components_h.h ${CMAKE_BINARY_DIR}/components.h
    ${CMAKE_BINARY_DIR}/commands_h.h ${CMAKE_BINARY_DIR}/commands.h
)
