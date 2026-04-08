#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>

#include "AccessibilityLayer.h"
#include "Adapter.h"
#include "CapabilityGraph.h"
#include "CliApp.h"
#include "EventBus.h"
#include "ExecutionEngine.h"
#include "IntentResolver.h"
#include "IntentRegistry.h"
#include "Logger.h"
#include "ObserverEngine.h"
#include "SystemWatchers.h"
#include "Telemetry.h"

int main(int argc, char* argv[]) {
    try {
        iee::EventBus eventBus;

        eventBus.Subscribe(iee::EventType::Error, [](const iee::Event& event) {
            iee::Logger::Error(event.source, event.message);
        });

        iee::AccessibilityLayer accessibilityLayer;
        iee::ObserverEngine observerEngine(accessibilityLayer);
        iee::Telemetry telemetry;

        iee::AdapterRegistry adapterRegistry;
        adapterRegistry.Register(std::make_unique<iee::VSCodeAdapter>(accessibilityLayer));
        adapterRegistry.Register(std::make_unique<iee::UIAAdapter>(accessibilityLayer));
        adapterRegistry.Register(std::make_unique<iee::InputAdapter>());
        adapterRegistry.Register(std::make_unique<iee::FileSystemAdapter>());

        iee::CapabilityGraphBuilder graphBuilder;
        iee::IntentResolver resolver;
        iee::IntentRegistry intentRegistry(observerEngine, adapterRegistry, graphBuilder, resolver, eventBus, telemetry);

        iee::IntentValidator validator;
        iee::ExecutionEngine executionEngine(adapterRegistry, eventBus, validator, telemetry);

        iee::UiChangeWatcher uiWatcher(eventBus);
        iee::FileSystemWatcher fileWatcher(std::filesystem::current_path().wstring(), eventBus);
        uiWatcher.Start();
        fileWatcher.Start();

        iee::CliApp app(intentRegistry, executionEngine, telemetry);
        const int exitCode = app.Run(argc, argv);

        fileWatcher.Stop();
        uiWatcher.Stop();

        return exitCode;
    } catch (const std::exception& ex) {
        iee::Logger::Error("main", ex.what());
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
}
