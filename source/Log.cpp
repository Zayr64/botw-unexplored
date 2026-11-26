#include "Log.h"

#include <iostream>
#include <fstream>
#include <dirent.h>

#include "SavefileIO.h"

#ifdef BOTW_COMPANION
#define LOG_FOLDER "sdmc:/switch/botw-companion"
#else
#define LOG_FOLDER "sdmc:/switch/botw-unexplored"
#endif

void LogInit()
{
    // Create main dir
    if (!SavefileIO::DirectoryExists(LOG_FOLDER))
        mkdir(LOG_FOLDER, 0777);
        
    // Clear the log
    std::ofstream file(LOG_FOLDER "/log.txt", std::ofstream::out | std::ofstream::trunc);
    file.close();
}

void Log(std::string thing)
{
    LogSave(thing);
}

void Log(std::string thing, std::string arg)
{
    std::string joined = thing + " " + arg;
    LogSave(joined);
}

void Log(std::string thing, int arg)
{
    std::string joined = thing + " " + std::to_string(arg);
    LogSave(joined);
}

void Log(std::string thing, float arg)
{
    std::string joined = thing + " " + std::to_string(arg);
    LogSave(joined);
}

void Log(int thing)
{
    LogSave(std::to_string(thing));
}

void Log(float thing)
{
    LogSave(std::to_string(thing));
}

void LogSave(std::string thing)
{
    // Print to nxlink console
    std::cout << thing << "\n";

    std::ofstream file(LOG_FOLDER "/log.txt", std::ios::app); // Open file at end
    if (file.is_open())
        file << thing << "\n";

    file.close();
}