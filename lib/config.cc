#include "lib/globals.h"
#include "lib/config.h"
#include "lib/proto.h"
#include "lib/logger.h"
#include "lib/utils.h"
#include "lib/imagewriter/imagewriter.h"
#include "lib/imagereader/imagereader.h"
#include "lib/fluxsink/fluxsink.h"
#include "lib/fluxsource/fluxsource.h"
#include "lib/encoders/encoders.h"
#include "lib/decoders/decoders.h"
#include <fstream>
#include <google/protobuf/text_format.h>
#include <regex>

static Config config;

struct FluxConstructor
{
    std::regex pattern;
    std::function<void(const std::string& filename, FluxSourceProto*)> source;
    std::function<void(const std::string& filename, FluxSinkProto*)> sink;
};

static const std::vector<FluxConstructor> fluxConstructors = {
    {.pattern = std::regex("^(.*\\.flux)$"),
     .source =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::FLUX);
            proto->mutable_fl2()->set_filename(s);
        }, .sink =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::FLUX);
            proto->mutable_fl2()->set_filename(s);
        }},
    {
     .pattern = std::regex("^(.*\\.scp)$"),
     .source =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::SCP);
            proto->mutable_scp()->set_filename(s);
        }, .sink =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::SCP);
            proto->mutable_scp()->set_filename(s);
        }, },
    {.pattern = std::regex("^(.*\\.a2r)$"),
     .source =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::A2R);
            proto->mutable_a2r()->set_filename(s);
        }, .sink =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::A2R);
            proto->mutable_a2r()->set_filename(s);
        }},
    {.pattern = std::regex("^(.*\\.cwf)$"),
     .source =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::CWF);
            proto->mutable_cwf()->set_filename(s);
        }},
    {.pattern = std::regex("^erase:$"),
     .source =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::ERASE);
        }},
    {.pattern = std::regex("^kryoflux:(.*)$"),
     .source =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::KRYOFLUX);
            proto->mutable_kryoflux()->set_directory(s);
        }},
    {.pattern = std::regex("^testpattern:(.*)"),
     .source =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::TEST_PATTERN);
        }},
    {.pattern = std::regex("^drive:(.*)"),
     .source =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::DRIVE);
            globalConfig().overrides()->mutable_drive()->set_drive(
                std::stoi(s));
        }, .sink =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::DRIVE);
            globalConfig().overrides()->mutable_drive()->set_drive(
                std::stoi(s));
        }},
    {.pattern = std::regex("^flx:(.*)$"),
     .source =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::FLX);
            proto->mutable_flx()->set_directory(s);
        }},
    {.pattern = std::regex("^vcd:(.*)$"),
     .sink =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::VCD);
            proto->mutable_vcd()->set_directory(s);
        }},
    {.pattern = std::regex("^au:(.*)$"),
     .sink =
            [](auto& s, auto* proto)
        {
            proto->set_type(FluxSourceSinkType::AU);
            proto->mutable_au()->set_directory(s);
        }},
};

Config& globalConfig()
{
    return config;
}

ConfigProto* Config::combined()
{
    if (!_configValid)
    {
        _combinedConfig = _baseConfig;

        /* First apply any standalone options. */

        std::set<std::string> options = _appliedOptions;
        std::set<const OptionPrerequisiteProto*> prereqs;
        for (const auto& option : _baseConfig.option())
        {
            if (options.find(option.name()) != options.end())
            {
                _combinedConfig.MergeFrom(option.config());
                options.erase(option.name());
            }
        }

        /* Then apply any group options. */

        for (auto& group : _baseConfig.option_group())
        {
            const OptionProto* selectedOption = &*group.option().begin();

            for (auto& option : group.option())
            {
                if (options.find(option.name()) != options.end())
                {
                    selectedOption = &option;
                    options.erase(option.name());
                }
            }

            _combinedConfig.MergeFrom(selectedOption->config());
        }

        /* Add in the user overrides. */

        _combinedConfig.MergeFrom(_overridesConfig);

        /* At this point the config is mostly valid. We're about to make calls
         * that will want to call combined() reentrantly, so to prevent infinite
         * loop we mark the config as valid now. */

        _configValid = true;

        /* We should now be more or less done, but we still need to add in any
         * config contributed by the flux source and image readers. This will
         * open the files. */

        if (hasFluxSource())
            _combinedConfig.MergeFrom(getFluxSource()->getExtraConfig());
        if (hasImageReader())
            _combinedConfig.MergeFrom(getImageReader()->getExtraConfig());

        /* Merge in the overrides once again. */

        _combinedConfig.MergeFrom(_overridesConfig);
    }
    return &_combinedConfig;
}

void Config::invalidate()
{
    _configValid = false;
}

void Config::clear()
{
    _configValid = false;
    _baseConfig.Clear();
    _overridesConfig.Clear();
    _combinedConfig.Clear();
    _fluxSource.reset();
    _verificationFluxSource.reset();
    _imageReader.reset();
    _encoder.reset();
    _decoder.reset();
    _appliedOptions.clear();
}

std::vector<std::string> Config::validate()
{
    std::vector<std::string> results;

    std::set<std::string> optionNames = _appliedOptions;
    std::set<const OptionProto*> appliedOptions;
    for (const auto& option : _baseConfig.option())
    {
        if (optionNames.find(option.name()) != optionNames.end())
        {
            appliedOptions.insert(&option);
            optionNames.erase(option.name());
        }
    }

    /* Then apply any group options. */

    for (auto& group : _baseConfig.option_group())
    {
        int count = 0;

        for (auto& option : group.option())
        {
            if (optionNames.find(option.name()) != optionNames.end())
            {
                optionNames.erase(option.name());
                appliedOptions.insert(&option);

                count++;
                if (count == 2)
                    results.push_back(
                        fmt::format("multiple mutually exclusive options set "
                                    "for group '{}'",
                            group.comment()));
            }
        }
    }

    /* Check for unknown options. */

    if (!optionNames.empty())
    {
        for (auto& name : optionNames)
            results.push_back(fmt::format("'{}' is not a known option", name));
    }

    /* Check option requirements. */

    for (auto& option : appliedOptions)
    {
        try
        {
            checkOptionValid(*option);
        }
        catch (const InapplicableOptionException& e)
        {
            results.push_back(e.message);
        }
    }

    return results;
}

void Config::validateAndThrow()
{
    auto r = validate();
    if (!r.empty())
    {
        std::stringstream ss;
        ss << "invalid configuration:\n";
        for (auto& s : r)
            ss << s << '\n';
        throw InapplicableOptionException(ss.str());
    }
}

void Config::set(std::string key, std::string value)
{
    setProtoByString(overrides(), key, value);
}

void Config::setTransient(std::string key, std::string value)
{
    setProtoByString(&_combinedConfig, key, value);
}

std::string Config::get(std::string key)
{
    return getProtoByString(combined(), key);
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
        if (!google::protobuf::TextFormat::MergeFromString(ss.str(), &config))
            error("couldn't load external config proto");
        return config;
    }
}

void Config::readBaseConfigFile(std::string filename)
{
    base()->MergeFrom(loadSingleConfigFile(filename));
}

void Config::readBaseConfig(std::string data)
{
    if (!google::protobuf::TextFormat::MergeFromString(data, base()))
        error("couldn't load external config proto");
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

    if (searchOptionList(base()->option()))
        return *found;

    for (const auto& optionGroup : base()->option_group())
    {
        if (searchOptionList(optionGroup.option()))
            return *found;
    }

    throw OptionNotFoundException(
        fmt::format("option {} not found", optionName));
}

void Config::checkOptionValid(const OptionProto& option)
{
    for (const auto& req : option.prerequisite())
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
            /* This field isn't available, therefore it
             * cannot match. */
        }

        if (!matched)
        {
            std::stringstream ss;
            ss << '[';
            bool first = true;
            for (auto requiredValue : req.value())
            {
                if (!first)
                    ss << ", ";
                ss << quote(requiredValue);
                first = false;
            }
            ss << ']';

            throw InapplicableOptionException(
                fmt::format("option '{}' is inapplicable to this "
                            "configuration "
                            "because {}={} could not be met",
                    option.name(),
                    req.key(),
                    ss.str()));
        }
    }
}

bool Config::isOptionValid(const OptionProto& option)
{
    try
    {
        checkOptionValid(option);
        return true;
    }
    catch (const InapplicableOptionException& e)
    {
        return false;
    }
}

bool Config::isOptionValid(std::string option)
{
    return isOptionValid(findOption(option));
}

void Config::applyOption(const OptionProto& option)
{
    log("OPTION: {}",
        option.has_message() ? option.message() : option.comment());

    _appliedOptions.insert(option.name());
}

void Config::applyOption(std::string option)
{
    applyOption(findOption(option));
}

void Config::clearOptions()
{
    _appliedOptions.clear();
    invalidate();
}

static void setFluxSourceImpl(std::string filename, FluxSourceProto* proto)
{
    for (const auto& it : fluxConstructors)
    {
        std::smatch match;
        if (std::regex_match(filename, match, it.pattern))
        {
            if (!it.source)
                throw new InapplicableValueException();
            it.source(match[1], proto);
            return;
        }
    }

    error("unrecognised flux filename '{}'", filename);
}

void Config::setFluxSource(std::string filename)
{
    setFluxSourceImpl(filename, overrides()->mutable_flux_source());
}

static void setFluxSinkImpl(std::string filename, FluxSinkProto* proto)
{
    for (const auto& it : fluxConstructors)
    {
        std::smatch match;
        if (std::regex_match(filename, match, it.pattern))
        {
            if (!it.sink)
                throw new InapplicableValueException();
            it.sink(match[1], proto);
            return;
        }
    }

    error("unrecognised flux filename '{}'", filename);
}

void Config::setFluxSink(std::string filename)
{
    setFluxSinkImpl(filename, overrides()->mutable_flux_sink());
}

void Config::setCopyFluxTo(std::string filename)
{
    setFluxSinkImpl(
        filename, overrides()->mutable_decoder()->mutable_copy_flux_to());
}

void Config::setVerificationFluxSource(std::string filename)
{
    setFluxSourceImpl(filename, &_verificationFluxSourceProto);
}

void Config::setImageReader(std::string filename)
{
    static const std::map<std::string, std::function<void(ImageReaderProto*)>>
        formats = {
  // clang-format off
		{".adf",      [](auto* proto) { proto->set_type(ImageReaderProto::IMG); }},
		{".d64",      [](auto* proto) { proto->set_type(ImageReaderProto::D64); }},
		{".d81",      [](auto* proto) { proto->set_type(ImageReaderProto::IMG); }},
		{".d88",      [](auto* proto) { proto->set_type(ImageReaderProto::D88); }},
		{".dim",      [](auto* proto) { proto->set_type(ImageReaderProto::DIM); }},
		{".diskcopy", [](auto* proto) { proto->set_type(ImageReaderProto::DISKCOPY); }},
		{".dsk",      [](auto* proto) { proto->set_type(ImageReaderProto::IMG); }},
		{".fdi",      [](auto* proto) { proto->set_type(ImageReaderProto::FDI); }},
		{".imd",      [](auto* proto) { proto->set_type(ImageReaderProto::IMD); }},
		{".img",      [](auto* proto) { proto->set_type(ImageReaderProto::IMG); }},
		{".jv3",      [](auto* proto) { proto->set_type(ImageReaderProto::JV3); }},
		{".nfd",      [](auto* proto) { proto->set_type(ImageReaderProto::NFD); }},
		{".nsi",      [](auto* proto) { proto->set_type(ImageReaderProto::NSI); }},
		{".st",       [](auto* proto) { proto->set_type(ImageReaderProto::IMG); }},
		{".td0",      [](auto* proto) { proto->set_type(ImageReaderProto::TD0); }},
		{".vgi",      [](auto* proto) { proto->set_type(ImageReaderProto::IMG); }},
		{".xdf",      [](auto* proto) { proto->set_type(ImageReaderProto::IMG); }},
  // clang-format on
    };

    for (const auto& it : formats)
    {
        if (endsWith(filename, it.first))
        {
            it.second(overrides()->mutable_image_reader());
            overrides()->mutable_image_reader()->set_filename(filename);
            return;
        }
    }

    error("unrecognised image filename '{}'", filename);
}

void Config::setImageWriter(std::string filename)
{
    static const std::map<std::string, std::function<void(ImageWriterProto*)>>
        formats = {
  // clang-format off
		{".adf",      [](auto* proto) { proto->set_type(ImageWriterProto::IMG); }},
		{".d64",      [](auto* proto) { proto->set_type(ImageWriterProto::D64); }},
		{".d81",      [](auto* proto) { proto->set_type(ImageWriterProto::IMG); }},
		{".d88",      [](auto* proto) { proto->set_type(ImageWriterProto::D88); }},
		{".diskcopy", [](auto* proto) { proto->set_type(ImageWriterProto::DISKCOPY); }},
		{".dsk",      [](auto* proto) { proto->set_type(ImageWriterProto::IMG); }},
		{".img",      [](auto* proto) { proto->set_type(ImageWriterProto::IMG); }},
		{".imd",      [](auto* proto) { proto->set_type(ImageWriterProto::IMD); }},
		{".ldbs",     [](auto* proto) { proto->set_type(ImageWriterProto::LDBS); }},
		{".nsi",      [](auto* proto) { proto->set_type(ImageWriterProto::NSI); }},
		{".raw",      [](auto* proto) { proto->set_type(ImageWriterProto::RAW); }},
		{".st",       [](auto* proto) { proto->set_type(ImageWriterProto::IMG); }},
		{".vgi",      [](auto* proto) { proto->set_type(ImageWriterProto::IMG); }},
		{".xdf",      [](auto* proto) { proto->set_type(ImageWriterProto::IMG); }},
  // clang-format on
    };

    for (const auto& it : formats)
    {
        if (endsWith(filename, it.first))
        {
            it.second(overrides()->mutable_image_writer());
            overrides()->mutable_image_writer()->set_filename(filename);
            return;
        }
    }

    error("unrecognised image filename '{}'", filename);
}

bool Config::hasFluxSource()
{
    return (*this)->flux_source().type() != FluxSourceSinkType::NOT_SET;
}

std::shared_ptr<FluxSource>& Config::getFluxSource()
{
    if (!_fluxSource)
    {
        if (!hasFluxSource())
            error("no flux source configured");

        _fluxSource =
            std::shared_ptr(FluxSource::create((*this)->flux_source()));
    }
    return _fluxSource;
}

bool Config::hasVerificationFluxSource() const
{
    return _verificationFluxSourceProto.type() != FluxSourceSinkType::NOT_SET;
}

std::shared_ptr<FluxSource>& Config::getVerificationFluxSource()
{
    if (!_verificationFluxSource)
    {
        if (!hasVerificationFluxSource())
            error("no verification flux source configured");

        _verificationFluxSource =
            std::shared_ptr(FluxSource::create(_verificationFluxSourceProto));
    }
    return _verificationFluxSource;
}

bool Config::hasImageReader()
{
    return (*this)->image_reader().type() != ImageReaderProto::NOT_SET;
}

std::shared_ptr<ImageReader>& Config::getImageReader()
{
    if (!_imageReader)
    {
        if (!hasImageReader())
            error("no image reader configured");

        _imageReader =
            std::shared_ptr(ImageReader::create((*this)->image_reader()));
    }
    return _imageReader;
}

bool Config::hasFluxSink()
{
    return (*this)->flux_sink().type() != FluxSourceSinkType::NOT_SET;
}

std::unique_ptr<FluxSink> Config::getFluxSink()
{
    if (!hasFluxSink())
        error("no flux sink configured");

    return FluxSink::create((*this)->flux_sink());
}

bool Config::hasImageWriter()
{
    return (*this)->image_writer().type() != ImageWriterProto::NOT_SET;
}

std::unique_ptr<ImageWriter> Config::getImageWriter()
{
    if (!hasImageWriter())
        error("no image writer configured");

    return ImageWriter::create((*this)->image_writer());
}

bool Config::hasEncoder()
{
    return (*this)->has_encoder();
}

std::shared_ptr<Encoder>& Config::getEncoder()
{
    if (!_encoder)
    {
        if (!hasEncoder())
            error("no encoder configured");

        _encoder = Encoder::create((*this)->encoder());
    }
    return _encoder;
}

bool Config::hasDecoder()
{
    return _combinedConfig.has_decoder();
}

std::shared_ptr<Decoder>& Config::getDecoder()
{
    if (!_decoder)
    {
        if (!hasDecoder())
            error("no decoder configured");

        _decoder = Decoder::create((*this)->decoder());
    }
    return _decoder;
}
