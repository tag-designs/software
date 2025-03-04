#ifndef CONFIG_INTERFACE_H
#define CONFIG_INTERFACE_H

/**
 * @brief Abstract class for a configuration interface
 * 
 */

class Config;
class ConfigInterface 
{

public:
    virtual bool Attach(const Config &config) = 0;  
    virtual void Detach() = 0;
    virtual bool SetConfig(const Config &config) = 0;
    virtual bool GetConfig(Config &config) = 0;
    bool isActive() {return active;}

protected:
    bool active;
};

#endif