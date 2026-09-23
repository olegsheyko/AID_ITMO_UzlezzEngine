#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include "core/Logger.h"
#include "core/Application.h"
#include "core/LaunchOptions.h"
#include <iostream>

// cmake --build build --config Release

int main(int argc, char** argv) {
	LaunchOptions options;
	std::string error;
	if (!parseLaunchOptions(argc, argv, options, error)) {
		std::cerr << error << "\n" << launchUsage() << "\n";
		return 2;
	}

	Logger::getInstance().openFile("engine.log");

	Application app;
	if (!app.init(800, 600, "Uzlezz Engine", options))
		return -1;

	app.run();
	app.shutdown();

    return 0;
}