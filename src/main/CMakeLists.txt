add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/components_h.h ${CMAKE_SOURCE_DIR}/components.h
                   COMMAND "$<TARGET_FILE:generate_components> src/include/components/ ${CMAKE_SOURCE_DIR}/components_h.h ${CMAKE_SOURCE_DIR}/components.h"
                   VERBATIM)
add_executable(generate_components ${CMAKE_SOURCE_DIR}/generate_components.cc)


add_executable(generate_commands ${CMAKE_SOURCE_DIR}/generate_commands.cc)

add_custom_command(OUTPUT ${CMAKE_SOURCE_DIR}/commands_h.h ${CMAKE_SOURCE_DIR}/commands.h
                   COMMAND "$<TARGET_FILE:generate_commands> src/main/commands/ ${CMAKE_SOURCE_DIR}/commands_h.h ${CMAKE_SOURCE_DIR}/commands.h"
                   VERBATIM)

add_library(main
    ComponentFactory.cc
    fileloaders/FileLoader_ELF.cc
    fileloaders/FileLoader_aout.cc
    fileloaders/FileLoader_raw.cc
    MemoryPassthrough.cc
    GXemul.cc
    StateVariable.cc
    Component.cc
    Command.cc
    StringHelper.cc
    EscapedString.cc
    FileLoader.cc
    debug_new.cc
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
)

add_dependencies(main generate_components generate_commands)