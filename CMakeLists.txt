cmake_minimum_required(VERSION 3.20)
project(IntentExecutionEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(NOMINMAX UNICODE _UNICODE)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(iee_core
    core/action/src/ActionInterface.cpp
    core/accessibility/src/AccessibilityLayer.cpp
    core/capability/src/CapabilityGraph.cpp
    core/capability/src/CapabilityExtractor.cpp
    core/event/src/EventBus.cpp
    core/event/src/Logger.cpp
    core/event/src/SystemWatchers.cpp
    core/execution/src/ActionSequence.cpp
    core/execution/src/Adapter.cpp
    core/execution/src/ControlRuntime.cpp
    core/execution/src/DecisionInterfaces.cpp
    core/execution/src/EnvironmentAdapter.cpp
    core/execution/src/ExecutionContract.cpp
    core/execution/src/ExecutionEngine.cpp
    core/execution/src/RevealExecutor.cpp
    core/execution/src/ScreenPerception.cpp
    core/intent/src/Intent.cpp
    core/intent/src/IntentNormalizer.cpp
    core/intent/src/IntentResolver.cpp
    core/intent/src/IntentRegistry.cpp
    core/interaction/src/AIStateView.cpp
    core/interaction/src/InteractionGraph.cpp
    core/interaction/src/TaskInterface.cpp
    core/observer/src/ObserverEngine.cpp
    core/telemetry/src/Telemetry.cpp
    interface/api/src/IntentApiServer.cpp
    interface/sdk/src/IEEClient.cpp
)

target_include_directories(iee_core PUBLIC
    core/action/include
    core/accessibility/include
    core/capability/include
    core/event/include
    core/execution/include
    core/intent/include
    core/interaction/include
    core/observer/include
    core/telemetry/include
    interface/api/include
    interface/cli/include
    interface/sdk/include
)

target_link_libraries(iee_core PUBLIC
    ole32
    oleaut32
    user32
    uiautomationcore
    d3d11
    dxgi
)

add_executable(iee
    main.cpp
    interface/cli/src/CliApp.cpp
    interface/cli/src/CliParser.cpp
)

target_link_libraries(iee PRIVATE iee_core ws2_32)

enable_testing()

add_executable(iee_test_unit_capability
    tests/unit_capability_extractor.cpp
)
target_include_directories(iee_test_unit_capability PRIVATE tests)
target_link_libraries(iee_test_unit_capability PRIVATE iee_core)

add_executable(iee_test_unit_intent
    tests/unit_intent_schema.cpp
)
target_include_directories(iee_test_unit_intent PRIVATE tests)
target_link_libraries(iee_test_unit_intent PRIVATE iee_core)

add_executable(iee_test_integration_execution
    tests/integration_execution_filesystem.cpp
)
target_include_directories(iee_test_integration_execution PRIVATE tests)
target_link_libraries(iee_test_integration_execution PRIVATE iee_core)

add_executable(iee_test_scenario_resolution
    tests/scenario_resolution.cpp
)
target_include_directories(iee_test_scenario_resolution PRIVATE tests)
target_link_libraries(iee_test_scenario_resolution PRIVATE iee_core)

add_executable(iee_test_unit_adapter_reliability
    tests/unit_adapter_reliability.cpp
)
target_include_directories(iee_test_unit_adapter_reliability PRIVATE tests)
target_link_libraries(iee_test_unit_adapter_reliability PRIVATE iee_core)

add_executable(iee_test_unit_telemetry
    tests/unit_telemetry.cpp
)
target_include_directories(iee_test_unit_telemetry PRIVATE tests)
target_link_libraries(iee_test_unit_telemetry PRIVATE iee_core)

add_executable(iee_test_unit_observation_pipeline
    tests/unit_observation_pipeline.cpp
)
target_include_directories(iee_test_unit_observation_pipeline PRIVATE tests)
target_link_libraries(iee_test_unit_observation_pipeline PRIVATE iee_core)

add_executable(iee_test_unit_screen_perception
    tests/unit_screen_perception.cpp
)
target_include_directories(iee_test_unit_screen_perception PRIVATE tests)
target_link_libraries(iee_test_unit_screen_perception PRIVATE iee_core)

add_executable(iee_test_unit_interaction_graph
    tests/unit_interaction_graph.cpp
)
target_include_directories(iee_test_unit_interaction_graph PRIVATE tests)
target_link_libraries(iee_test_unit_interaction_graph PRIVATE iee_core)

add_executable(iee_test_integration_failure_injection
    tests/integration_failure_injection.cpp
)
target_include_directories(iee_test_integration_failure_injection PRIVATE tests)
target_link_libraries(iee_test_integration_failure_injection PRIVATE iee_core)

add_executable(iee_test_integration_api_hardening
    tests/integration_api_hardening.cpp
)
target_include_directories(iee_test_integration_api_hardening PRIVATE tests)
target_link_libraries(iee_test_integration_api_hardening PRIVATE iee_core)

add_executable(iee_test_stress_execution
    tests/stress_execution_loop.cpp
)
target_include_directories(iee_test_stress_execution PRIVATE tests)
target_link_libraries(iee_test_stress_execution PRIVATE iee_core)

add_executable(iee_test_stress_screen_pipeline
    tests/stress_screen_pipeline.cpp
)
target_include_directories(iee_test_stress_screen_pipeline PRIVATE tests)
target_link_libraries(iee_test_stress_screen_pipeline PRIVATE iee_core)

add_executable(iee_test_integration_control_runtime
    tests/integration_control_runtime.cpp
)
target_include_directories(iee_test_integration_control_runtime PRIVATE tests)
target_link_libraries(iee_test_integration_control_runtime PRIVATE iee_core)

add_executable(iee_test_integration_closed_loop_feedback
    tests/integration_closed_loop_feedback.cpp
)
target_include_directories(iee_test_integration_closed_loop_feedback PRIVATE tests)
target_link_libraries(iee_test_integration_closed_loop_feedback PRIVATE iee_core)

add_executable(iee_test_scenario_uig_hidden_exposure
    tests/scenario_uig_hidden_exposure.cpp
)
target_include_directories(iee_test_scenario_uig_hidden_exposure PRIVATE tests)
target_link_libraries(iee_test_scenario_uig_hidden_exposure PRIVATE iee_core)

add_executable(iee_test_scenario_task_interface
    tests/scenario_task_interface.cpp
)
target_include_directories(iee_test_scenario_task_interface PRIVATE tests)
target_link_libraries(iee_test_scenario_task_interface PRIVATE iee_core)

add_executable(iee_test_integration_action_interface
    tests/integration_action_interface.cpp
)
target_include_directories(iee_test_integration_action_interface PRIVATE tests)
target_link_libraries(iee_test_integration_action_interface PRIVATE iee_core)

add_executable(iee_test_scenario_action_interface
    tests/scenario_action_interface.cpp
)
target_include_directories(iee_test_scenario_action_interface PRIVATE tests)
target_link_libraries(iee_test_scenario_action_interface PRIVATE iee_core)

add_test(NAME unit_capability_extractor COMMAND iee_test_unit_capability)
add_test(NAME unit_intent_schema COMMAND iee_test_unit_intent)
add_test(NAME integration_execution_filesystem COMMAND iee_test_integration_execution)
add_test(NAME scenario_resolution COMMAND iee_test_scenario_resolution)
add_test(NAME unit_adapter_reliability COMMAND iee_test_unit_adapter_reliability)
add_test(NAME unit_telemetry COMMAND iee_test_unit_telemetry)
add_test(NAME unit_observation_pipeline COMMAND iee_test_unit_observation_pipeline)
add_test(NAME unit_screen_perception COMMAND iee_test_unit_screen_perception)
add_test(NAME unit_interaction_graph COMMAND iee_test_unit_interaction_graph)
add_test(NAME integration_failure_injection COMMAND iee_test_integration_failure_injection)
add_test(NAME integration_api_hardening COMMAND iee_test_integration_api_hardening)
add_test(NAME stress_execution_loop COMMAND iee_test_stress_execution)
add_test(NAME stress_screen_pipeline COMMAND iee_test_stress_screen_pipeline)
add_test(NAME integration_control_runtime COMMAND iee_test_integration_control_runtime)
add_test(NAME integration_closed_loop_feedback COMMAND iee_test_integration_closed_loop_feedback)
add_test(NAME scenario_uig_hidden_exposure COMMAND iee_test_scenario_uig_hidden_exposure)
add_test(NAME scenario_task_interface COMMAND iee_test_scenario_task_interface)
add_test(NAME integration_action_interface COMMAND iee_test_integration_action_interface)
add_test(NAME scenario_action_interface COMMAND iee_test_scenario_action_interface)
