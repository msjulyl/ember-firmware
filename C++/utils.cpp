/* 
 * File:   utils.cpp
 * Author: Richard Greene
 * 
 * Some handy utilities
 *
 * Created on April 17, 2014, 3:16 PM
 */

#include <time.h>
#include <stdio.h>
#include <ctype.h>
#include <string>
#include <fcntl.h>
#include <dirent.h>

#include <Filenames.h>
#include <Logger.h>

/// Get the current time in millliseconds
long GetMillis(){
    struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
    // printf("time = %d sec + %ld nsec\n", now.tv_sec, now.tv_nsec);
    return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

long startTime = 0;

/// Start the stopwatch timer
void StartStopwatch()
{
    startTime = GetMillis();    
}

/// Stop the stopwatch and return its time in millliseconds
long StopStopwatch()
{
    return GetMillis() - startTime;
}

/// Convert the given string to upper case, and terminate it at whitespace
char* CmdToUpper(char* cmd)
{
    char* p = cmd;
    while(*p != 0)
    {
        *p++ = toupper(*p);
        if(isspace(*p))
            *p = 0;
    }
    return cmd;
}


/// Replace all instances of the oldVal in str with the newVal
std::string Replace(std::string str, const char* oldVal, const char* newVal)
{
    int pos = 0; 
    while((pos = str.find(oldVal)) != std::string::npos)
         str.replace(pos, 1, newVal); 
    
    return str;
}

/// Get the version string for this firmware.  Currently we just return a 
/// string constant, but this wrapper allows for alternate implementations.
const char* GetFirmwareVersion()
{
    return FIRMWARE_VERSION;
}

/// Get the board serial number.  Currently we just return the main Sitara 
/// board's serial no., but this wrapper allows for alternate implementations.
const char* GetBoardSerialNum()
{
    static char serialNo[14] = {0};
    if(serialNo[0] == 0)
    {
        memset(serialNo, 0, 14);
        int fd = open(BOARD_SERIAL_NUM_FILE, O_RDONLY);
        if(fd < 0 || lseek(fd, 16, SEEK_SET) != 16
                  || read(fd, serialNo, 12) != 12)
            LOGGER.LogError(LOG_ERR, errno, SERIAL_NUM_ACCESS_ERROR);
        serialNo[12] = '\n';
    }
    return serialNo;
}

void PurgeDirectory(std::string path)
{
    struct dirent* nextFile;
    DIR* folder;
    char filePath[PATH_MAX];
    
    folder = opendir(path.c_str());
    
    while (nextFile = readdir(folder))
    {
        sprintf(filePath, "%s/%s", path.c_str(), nextFile->d_name);
        remove(filePath);
    }
}