// The Log() of source/Log.h for this app: sdmc:/switch/botw-cozinha/log.txt, emptied at each start.
#include "Log.h"

#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace
{
    const char* LogPath = "sdmc:/switch/botw-cozinha/log.txt";
}

void LogInit()
{
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/botw-cozinha", 0777);
    FILE* f = fopen(LogPath, "w");
    if (f)
        fclose(f);
}

void LogSave(std::string thing)
{
    FILE* f = fopen(LogPath, "a");
    if (!f)
        return;
    fprintf(f, "%s\n", thing.c_str());
    fclose(f);
    printf("%s\n", thing.c_str());
}

void Log(std::string thing) { LogSave(thing); }
void Log(std::string thing, std::string arg) { LogSave(thing + " " + arg); }
void Log(std::string thing, int arg) { LogSave(thing + " " + std::to_string(arg)); }
void Log(std::string thing, float arg) { LogSave(thing + " " + std::to_string(arg)); }
void Log(int thing) { LogSave(std::to_string(thing)); }
void Log(float thing) { LogSave(std::to_string(thing)); }
