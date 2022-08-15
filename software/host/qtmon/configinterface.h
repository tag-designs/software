#ifndef CONFIG_INTERFACE_H
#define CONFIG_INTERFACE_H

class Config;

class ConfigInterface
{

public:
    virtual ~ConfigInterface(){};

    virtual void Attach(const Config &config) = 0;  
    virtual void SetConfig(const Config &config) = 0;
    virtual void GetConfig(Config &config) = 0;
    virtual QWidget *GetWidget() = 0;    
};

Q_DECLARE_INTERFACE(ConfigInterface,"ConfigInterface")


#endif