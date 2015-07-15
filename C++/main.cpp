/* 
 * File:   main.cpp
 * Author: Richard Greene
 *
 * Created on March 26, 2014, 4:01 PM
 */

#include <iostream>
#include <string>
#include <utils.h>
#include <signal.h>
#include <fcntl.h>

#include <PrintEngine.h>
#include <EventHandler.h>
#include <TerminalUI.h>
#include <Logger.h>
#include <NetworkInterface.h>
#include <CommandInterpreter.h>
#include <Settings.h>
#include <MessageStrings.h>
#include <Hardware.h>
#include <MotorController.h>
#include <Filenames.h>

using namespace std;

// command line argument to suppress use of stdin & stdout
#define NO_STDIO    "--nostdio"

int main(int argc, char** argv) 
{
    // Set up signal handling
    struct sigaction exitSA, hangUpSA;

    // Call exit handler on SIGINT or SIGTERM
    sigemptyset(&exitSA.sa_mask);
    exitSA.sa_handler = &ExitHandler;
    exitSA.sa_flags = 0;
    sigaddset(&exitSA.sa_mask, SIGINT);
    sigaddset(&exitSA.sa_mask, SIGTERM);
    sigaction(SIGINT, &exitSA, NULL);
    sigaction(SIGTERM, &exitSA, NULL);
    
    // Ignore SIGHUP as it causes termination by default
    sigemptyset(&hangUpSA.sa_mask);
    hangUpSA.sa_handler = SIG_IGN;
    hangUpSA.sa_flags = 0;
    sigaction(SIGHUP, &hangUpSA, NULL);

    // see if we should support keyboard input and TerminalUI output
    bool useStdio = true;
    if(argc > 1) 
    {
        useStdio = strcmp(argv[1], NO_STDIO) != 0;
    }
    
    // report the firmware version, board serial number, and startup message
    LOGGER.LogMessage(LOG_INFO, PRINTER_STARTUP_MSG);
    string fwVersion = string(FW_VERSION_MSG) + GetFirmwareVersion();
    LOGGER.LogMessage(LOG_INFO, fwVersion.c_str());
    string serNum = string(BOARD_SER_NUM_MSG) + GetBoardSerialNum();
    LOGGER.LogMessage(LOG_INFO, serNum.c_str());
   
    if (useStdio)
    {
        cout << PRINTER_STARTUP_MSG << endl;
        cout << fwVersion << serNum;
    }
       
    // use cape manager to enable non-default I/O
    int fd = open(CAPE_MANAGER_SLOTS_FILE, O_WRONLY); 
    if(fd < 0)
    {
        LOGGER.LogError(LOG_ERR, errno, ERR_MSG(CantOpenCapeManager), 
                                                CAPE_MANAGER_SLOTS_FILE);
        exit(1);
    }

    
    std::string s[] = {"am33xx_pwm",    // enable PWM outputs to fans
                       "bone_pwm_P8_19",   
                       "bone_pwm_P9_16", 
                       "bone_pwm_P8_13" };

    for(int i = 0; i < sizeof(s)/sizeof(std::string); i++)
        write(fd, s[i].c_str(), s[i].size());
    
    close(fd);
         
    // ensure directories exist
    MakePath(SETTINGS.GetString(PRINT_DATA_DIR));
    MakePath(SETTINGS.GetString(DOWNLOAD_DIR));
    MakePath(SETTINGS.GetString(STAGING_DIR));

    // Declare EventHandler, PrintEngine, and FrontPanel instances as static 
    // so destructors are called when exit() is called
    // create an event handler
    static EventHandler eh;
    
    // create a print engine that communicates with actual hardware
    static PrintEngine pe(true);

    // give it to the settings singleton as an error handler
    SETTINGS.SetErrorHandler(&pe);
    
    // create the front panel
    int port = (SETTINGS.GetInt(HARDWARE_REV) == 0) ? I2C2_PORT : I2C1_PORT;
    static FrontPanel fp(UI_SLAVE_ADDRESS, port); 
    // set the screensaver time
    fp.SetAwakeTime(SETTINGS.GetInt(FRONT_PANEL_AWAKE_TIME));
 
    // set the I2C devices
    eh.SetI2CDevice(MotorInterrupt, pe.GetMotorController(), MC_STATUS_REG);
    eh.SetI2CDevice(MotorTimeout,   pe.GetMotorController(), MC_STATUS_REG);
    eh.SetI2CDevice(ButtonInterrupt, &fp, BTN_STATUS);
    
    // subscribe logger singleton first, so that it will show 
    // its output in the logs ahead of any other subscribers that actually 
    // act on those events
    eh.Subscribe(PrinterStatusUpdate, &LOGGER);
    eh.Subscribe(MotorInterrupt, &LOGGER);
    eh.Subscribe(ButtonInterrupt, &LOGGER);
    eh.Subscribe(DoorInterrupt, &LOGGER);
    if(useStdio)
        eh.Subscribe(Keyboard, &LOGGER);
    eh.Subscribe(UICommand, &LOGGER);
    
    // subscribe the print engine to interrupt events
    eh.Subscribe(MotorInterrupt, &pe);
    eh.Subscribe(ButtonInterrupt, &pe); 
    eh.Subscribe(DoorInterrupt, &pe);
    eh.Subscribe(RotationInterrupt, &pe);
    
    // subscribe the print engine to timer events
    eh.SetFileDescriptor(DelayEnd, pe.GetDelayTimerFD());
    eh.Subscribe(DelayEnd, &pe);
    
    eh.SetFileDescriptor(ExposureEnd, pe.GetExposureTimerFD());
    eh.Subscribe(ExposureEnd, &pe);
    
    eh.SetFileDescriptor(MotorTimeout, pe.GetMotorTimeoutTimerFD());
    eh.Subscribe(MotorTimeout, &pe);
    
    eh.SetFileDescriptor(TemperatureTimer, pe.GetTemperatureTimerFD());
    eh.Subscribe(TemperatureTimer, &pe);
    
    CommandInterpreter peCmdInterpreter(&pe);
    // subscribe the command interpreter to command input events,
    // from UI and possibly the keyboard
    eh.Subscribe(UICommand, &peCmdInterpreter); 
    if(useStdio)
        eh.Subscribe(Keyboard, &peCmdInterpreter);   
    
    // subscribe the front panel to printer status events
    eh.SetFileDescriptor(PrinterStatusUpdate, pe.GetStatusUpdateFD()); 
    eh.Subscribe(PrinterStatusUpdate, &fp);
    
    // also connect a network interface, subscribed to printer status events
    NetworkInterface networkIF;
    eh.Subscribe(PrinterStatusUpdate, &networkIF);
    
    if(useStdio)
    {
        // also connect a terminal UI, subscribed to printer status events
        TerminalUI terminal;
        eh.Subscribe(PrinterStatusUpdate, &terminal);
    }
    
    // start the print engine's state machine
    pe.Begin();
    // begin handling events
    eh.Begin();
    
    return 0;
}

