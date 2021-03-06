#include "config.hpp"

#include "quality.hpp"

#include "include/cef_version.h"

#include <Poco/Net/HTTPServer.h>

const char* BrowserviceVersion = "0.9.1.2";

namespace {

#define CONF_OPT_TYPE(var) \
    remove_const<remove_reference<decltype(declval<Config>().*(&Config::var))>::type>::type

template <typename T>
struct OptParser;

template <>
struct OptParser<string> {
    static optional<string> parse(const string& str) {
        return str;
    }
};

template <>
struct OptParser<bool> {
    static optional<bool> parse(string str) {
        for(char& c : str) {
            c = tolower(c);
        }
        if(
            str == "1" ||
            str == "yes" ||
            str == "true" ||
            str == "enable" ||
            str == "enabled"
        ) {
            return true;
        }
        if(
            str == "0" ||
            str == "no" ||
            str == "false" ||
            str == "disable" ||
            str == "disabled"
        ) {
            return false;
        }
        optional<bool> empty;
        return empty;
    }
};


template <>
struct OptParser<int> {
    static optional<int> parse(const string& str) {
        return parseString<int>(str);
    }
};

template <typename T>
struct DefaultValFormatter {
    static string format(const T& val) {
        return toString(val);
    }
};

template <>
struct DefaultValFormatter<bool> {
    static string format(bool val) {
        return val ? "yes" : "no";
    }
};

template <typename T, const T Config::* Var, typename S>
struct OptInfoBase {
    string defaultValStr() {
        return "default: " + DefaultValFormatter<T>::format(static_cast<S*>(this)->defaultVal());
    }
    optional<T> parse(const string& str) {
        return OptParser<T>::parse(str);
    }
    bool validate(const T& val) {
        return true;
    }
};

template <typename T, const T Config::* Var>
struct OptInfo;

#define CONF_DEF_OPT_INFO(var) \
    template <> \
    struct OptInfo<CONF_OPT_TYPE(var), &Config::var> : \
        public OptInfoBase< \
            CONF_OPT_TYPE(var), \
            &Config::var, \
            OptInfo<CONF_OPT_TYPE(var), &Config::var> \
        >

#include "config_defs.hpp"

#define CONF_OPT_INFO(var) \
    OptInfo<CONF_OPT_TYPE(var), &Config::var>()

template <typename T, const T Config::* Var>
string helpLine(OptInfo<T, Var> info) {
    const int DescStart = 33;
    const int DescStartIndented = 35;
    const int MaxWidth = 90;

    stringstream ss;
    ss << "  --" << info.name << "=" << info.valSpec << ' ';
    while(ss.tellp() < DescStart) {
        ss << ' ';
    }
    int linePos = ss.tellp();

    auto writeAtom = [&](string atom) {
        if(linePos + (int)atom.size() > MaxWidth && linePos > DescStartIndented) {
            ss << '\n';
            for(int i = 0; i < DescStartIndented; ++i) {
                ss << ' ';
            }
            linePos = DescStartIndented;

            int s = 0;
            while(s < (int)atom.size() && isspace(atom[s])) {
                ++s;
            }
            atom = atom.substr(s);
        }
        ss << atom;
        linePos += (int)atom.size();
    };

    string desc = info.desc();
    int a = 0;
    while(a < (int)desc.size()) {
        int b = a + 1;
        while(b < (int)desc.size() && !isspace(desc[b])) {
            ++b;
        }
        writeAtom(desc.substr(a, b - a));
        a = b;
    }
    writeAtom(" [" + info.defaultValStr() + "]");

    return ss.str();
}

}

class Config::Src {
public:
    Src()
        : dummy_(0)
          #define CONF_FOREACH_OPT_ITEM(var) \
              ,var(CONF_OPT_INFO(var).defaultVal())
          CONF_FOREACH_OPT
          #undef CONF_FOREACH_OPT_ITEM
    {}

private:
    int dummy_;

public:
    #define CONF_FOREACH_OPT_ITEM(var) \
        CONF_OPT_TYPE(var) var;
    CONF_FOREACH_OPT
    #undef CONF_FOREACH_OPT_ITEM
};

Config::Config(CKey, Src& src)
    : dummy_(0)
      #define CONF_FOREACH_OPT_ITEM(var) \
          ,var(move(src.var))
      CONF_FOREACH_OPT
      #undef CONF_FOREACH_OPT_ITEM
{}

shared_ptr<Config> Config::read(int argc, char* argv[]) {
    REQUIRE(argc >= 1);

    Src src;

    map<string, function<bool(string)>> optHandlers;
    #define CONF_FOREACH_OPT_ITEM(var) \
        optHandlers[CONF_OPT_INFO(var).name] = [&src](const string& valStr) { \
            typedef CONF_OPT_TYPE(var) T; \
            optional<T> val = CONF_OPT_INFO(var).parse(valStr); \
            if(val && CONF_OPT_INFO(var).validate((const T&)*val)) { \
                src.var = move(*val); \
                return true; \
            } else { \
                return false; \
            } \
        };
    CONF_FOREACH_OPT
    #undef CONF_FOREACH_OPT_ITEM

    set<string> optsSeen;

    for(int argi = 1; argi < argc; ++argi) {
        string arg = argv[argi];
        if(arg == "--help") {
            cout << "USAGE: " << argv[0] << " [OPTION]...\n";
            cout << "\n";
            cout << "Supported options:\n";

            vector<string> lines;

            #define CONF_FOREACH_OPT_ITEM(var) \
                lines.push_back(helpLine(CONF_OPT_INFO(var)));
            CONF_FOREACH_OPT
            #undef CONF_FOREACH_OPT_ITEM
            lines.push_back("  --help                         show this help and exit");
            lines.push_back("  --version                      show the version and exit");

            sort(lines.begin(), lines.end());
            for(const string& line : lines) {
                cout << line << '\n';
            }
            
            return {};
        }
        if(arg == "--version") {
            cout << "Browservice " << BrowserviceVersion << ", ";
            cout << "built with CEF " << CEF_VERSION << "\n";
            return {};
        }

        if(arg.substr(0, 2) == "--") {
            int eqSignPos = 2;
            while(eqSignPos < (int)arg.size() && arg[eqSignPos] != '=') {
                ++eqSignPos;
            }
            if(eqSignPos < (int)arg.size()) {
                string optName = arg.substr(2, eqSignPos - 2);
                auto it = optHandlers.find(optName);
                if(it != optHandlers.end()) {
                    if(!optsSeen.insert(optName).second) {
                        cerr << "ERROR: Option --" << optName << " specified multiple times\n";
                        return {};
                    }

                    string optVal = arg.substr(eqSignPos + 1);
                    bool result = it->second(optVal);
                    if(!result) {
                        cerr << "ERROR: Invalid value '" << optVal << "' given for option --" << optName << "\n";
                        cerr << "See '" << argv[0] << " --help' for more information\n";
                        return {};
                    }
                    continue;
                }
            }
        }

        if(arg.substr(0, 2) == "--" && optHandlers.count(arg.substr(2))) {
            cerr << "ERROR: Value missing for option " << arg << "\n";
        } else {
            cerr << "ERROR: Unrecognized option '" << arg << "'\n";
        }
        cerr << "Try '" << argv[0] << " --help' for list of supported options\n";
        return {};
    }
    return Config::create(src);
}
