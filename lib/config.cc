#include "lib/globals.h"
#include "lib/config.h"
#include "lib/proto.h"
#include "lib/logger.h"
#include <fstream>
#include <google/protobuf/text_format.h>

static Config config;

Config& globalConfig()
{
    return config;
}

ConfigProto* Config::operator->() const
{
    return &globalConfigProto();
}

Config::operator ConfigProto*() const
{
    return &globalConfigProto();
}

Config::operator ConfigProto&() const
{
    return globalConfigProto();
}

void Config::clear()
{
    (*this)->Clear();
}

void Config::set(std::string key, std::string value)
{
    setProtoByString(*this, key, value);
}

std::string Config::get(std::string key)
{
    return getProtoByString(*this, key);
}

static ConfigProto loadSingleConfigFile(std::string filename)
{
    const auto& it = formats.find(filename);
    if (it != formats.end())
        return *it->second;
    else
    {
        std::ifstream f(filename, std::ios::out);
        if (f.fail())
            error("Cannot open '{}': {}", filename, strerror(errno));

        std::ostringstream ss;
        ss << f.rdbuf();

        ConfigProto config;
        if (!google::protobuf::TextFormat::MergeFromString(
                ss.str(), globalConfig()))
            error("couldn't load external config proto");
        return config;
    }
}

void Config::readConfigFile(std::string filename)
{
    globalConfig()->MergeFrom(loadSingleConfigFile(filename));
}

const OptionProto& Config::findOption(const std::string& optionName)
{
    const OptionProto* found = nullptr;

    auto searchOptionList = [&](auto& optionList)
    {
        for (const auto& option : optionList)
        {
            if (optionName == option.name())
            {
                found = &option;
                return true;
            }
        }
        return false;
    };

    if (searchOptionList((*this)->option()))
        return *found;

    for (const auto& optionGroup : (*this)->option_group())
    {
        if (searchOptionList(optionGroup.option()))
            return *found;
    }

    throw OptionNotFoundException("option name not found");
}

bool Config::isOptionValid(const OptionProto& option)
{
    for (const auto& req : option.requires())
    {
        bool matched = false;
        try
        {
            auto value = get(req.key());
            for (auto requiredValue : req.value())
                matched |= (requiredValue == value);
        }
        catch (const ProtoPathNotFoundException e)
        {
            /* This field isn't available, therefore it cannot match. */
        }

        if (!matched)
            return false;
    }

    return true;
}

void Config::applyOption(const OptionProto& option)
{
    if (option.config().option_size() > 0)
        throw InvalidOptionException(fmt::format(
            "option '{}' has an option inside it, which isn't allowed",
            option.name()));
    if (option.config().option_group_size() > 0)
        throw InvalidOptionException(fmt::format(
            "option '{}' has an option group inside it, which isn't allowed",
            option.name()));
    if (!isOptionValid(option))
        throw InapplicableOptionException(
            fmt::format("option '{}' is inapplicable to this configuration",
                option.name()));

    log("OPTION: {}",
        option.has_message() ? option.message() : option.comment());

    (*this)->MergeFrom(option.config());
}
