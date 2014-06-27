/* 
 * File:   NetworkInterface.cpp
 * Author: Richard Greene
 *
 * Connects the Internet to the EventHandler.
 * 
 * Created on May 14, 2014, 5:45 PM
 */

#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/exceptions.hpp>

#include <NetworkInterface.h>
#include <Logger.h>
#include <Filenames.h>
#include <utils.h>

using boost::property_tree::ptree;
using boost::property_tree::ptree_error;

/// Constructor
NetworkInterface::NetworkInterface() :
_statusJSON("\n")
{
    // create the named pipe for reporting status to the web
    // don't recreate the FIFO if it exists already
    if (access(STATUS_TO_WEB_PIPE, F_OK) == -1) {
        if (mkfifo(STATUS_TO_WEB_PIPE, 0666) < 0) {
          LOGGER.LogError(LOG_ERR, errno, STATUS_TO_WEB_PIPE_CREATION_ERROR);
          // we can't really run if we can't update web clients on status
          exit(-1);  
        }
    }
    // Open both ends within this process in non-blocking mode,
    // otherwise open call would wait till other end of pipe
    // is opened by another process
    // no need to save read fd, since only the web reads from it
    open(STATUS_TO_WEB_PIPE, O_RDONLY|O_NONBLOCK);
    _statusPushFd = open(STATUS_TO_WEB_PIPE, O_WRONLY|O_NONBLOCK);
    
    // TODO: if/when other components need to respond to commands, the 
    // COMMAND_RESPONSE_PIPE should perhaps be created elsewhere
    // create the named pipe for reporting command responses to the web
    // don't recreate the FIFO if it exists already
    if (access(COMMAND_RESPONSE_PIPE, F_OK) == -1) {
        if (mkfifo(COMMAND_RESPONSE_PIPE, 0666) < 0) {
          LOGGER.LogError(LOG_ERR, errno, COMMAND_RESPONSE_PIPE_CREATION_ERROR);
          // we can't really run if we can't respond to commands
          exit(-1);  
        }
    }
    // no need to save read fd, since only the web reads from it
    open(COMMAND_RESPONSE_PIPE, O_RDONLY|O_NONBLOCK);
    _commandResponseFd = open(COMMAND_RESPONSE_PIPE, O_WRONLY|O_NONBLOCK);

}

/// Destructor, cleans up pipe used to communicate with webserver
NetworkInterface::~NetworkInterface()
{
    if (access(STATUS_TO_WEB_PIPE, F_OK) != -1)
        remove(STATUS_TO_WEB_PIPE);
}

/// Handle printer status updates and requests to report that status
void NetworkInterface::Callback(EventType eventType, void* data)
{     
    switch(eventType)
    {               
        case PrinterStatusUpdate:
            SaveCurrentStatus((PrinterStatus*)data);
            SendCurrentStatus(_statusPushFd);
            break;
            
        case UICommand:
            // we only handle the GetStatus command
            // TODO: we should be able to use a CommandInterpreter too, no?
            // we should not have to spell out this string here
            if(strcmp(CmdToUpper((char*)data), "GETSTATUS") == 0)
                SendCurrentStatus(_commandResponseFd);
            break;
            
        default:
            HandleImpossibleCase(eventType);
            break;
    }
}

/// Save the current printer status in JSON.
void NetworkInterface::SaveCurrentStatus(PrinterStatus* pStatus)
{
    try
    {
        ptree pt;
        std::string root = PRINTER_STATUS_KEY ".";
        
        pt.put(root + STATE_PS_KEY, pStatus->_state);
        
        const char* change = "none";
        if(pStatus->_change == Entering)
           change = "entering";
        else if(pStatus->_change == Leaving)
           change = "leaving";            
        pt.put(root + CHANGE_PS_KEY, change);
        pt.put(root + IS_ERROR_PS_KEY, pStatus->_isError);
        pt.put(root + ERROR_CODE_PS_KEY, pStatus->_errorCode); 
        pt.put(root + ERROR_PS_KEY, pStatus->_errorMessage);
        pt.put(root + LAYER_PS_KEY, pStatus->_currentLayer);
        pt.put(root + TOAL_LAYERS_PS_KEY, pStatus->_numLayers);
        pt.put(root + SECONDS_LEFT_PS_KEY, pStatus->_estimatedSecondsRemaining);
        pt.put(root + JOB_NAME_PS_KEY, pStatus->_jobName);
        pt.put(root + TEMPERATURE_PS_KEY, pStatus->_temperature);
        
        std::stringstream ss;
        write_json(ss, pt);  
        _statusJSON = ss.str();
           
        // remove newlines but add one back at end
        _statusJSON = Replace(_statusJSON, "\n", "") + "\n";
    }
    catch(ptree_error&)
    {
        LOGGER.LogError(LOG_ERR, errno, STATUS_JSON_SAVE_ERROR);       
    }
}

/// Write the latest printer status to the given pipe
void NetworkInterface::SendCurrentStatus(int fileDescriptor)
{
    try
    {
        lseek(fileDescriptor, 0, SEEK_SET);
        write(fileDescriptor, _statusJSON.c_str(), _statusJSON.length());
    }
    catch(...)
    {
        LOGGER.LogError(LOG_ERR, errno, STATUS_JSON_SEND_ERROR);
    }
}