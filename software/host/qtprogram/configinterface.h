#ifndef CONFIG_INTERFACE_H
#define CONFIG_INTERFACE_H

#include <tagclass.h>

/**
 * @file configinterface.h
 * @brief Abstract class for a configuration interface
 * 
 */

class Config;
class ConfigInterface 
{

public:
    /**
     * @brief Attach is called when a tag is first connected; 
     * 
     * @param tag being attached to to be applied to program state
     * @return true if no error occurred
     * @return false if an error occurred
     */
    virtual bool Attach(Tag &tag) = 0;  

    /**
     * @brief Detach is called when the tag is disconnected; subclasses
     *       may need to reset/clean up internal state
     * 
     */
    virtual void Detach() = 0;
    /**
     * @brief Set the Config object
     * 
     * @param config configuration to assign to program state
     * @return true  if there were no configuration errors
     * @return false if there was a configuration error
     */
    virtual bool SetConfig(const Config &config) = 0;
    /**
     * @brief Get the Config object
     * 
     * @param config container to copy program state into
     * @return true if there were no errors
     * @return false  if there was an error
     */
    virtual bool GetConfig(Config &config) = 0;
    /**
     * @brief active if there is some configurable state and no errors in configuration
     * 
     * @return true no errors and there is configured state
     * @return false there is no configured state or there were configuration errors
     */
    bool isActive() {return active;}

protected:
    bool active;
};

#endif