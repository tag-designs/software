#include <stdint.h>
#include <iostream>
#include <fstream>
#include <google/protobuf/util/json_util.h>
#include <string>
#include <vector>

#include <tag.pb.h>

using namespace std;
using namespace google::protobuf;
using namespace google::protobuf::util;

int main(int argc, char *argv[]) {
    Config configin;
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " json-file [output-file]" << endl;
        return 1;
    } else {
        ifstream fin(argv[1]);
        if (!fin.is_open())
        {
            cerr << "Couldn't open " << argv[1] << endl;
            return 1;
        }

        string str((istreambuf_iterator<char>(fin)),
                  istreambuf_iterator<char>());
        fin.close();


        google::protobuf::util::JsonParseOptions options2;
        auto err = JsonStringToMessage(str, &configin, options2);
        if (err.ok()) {
            ofstream fout;
            ostream *out = &cout;
            if (argc > 2) {
                fout.open(argv[2]);
                if (!fout.is_open()) {
                    cerr << "Couldn't open " << argv[2] << endl;
                    return 1;
                }
                out = &fout;
            }

            string output;
            configin.SerializeToString(&output);
            *out << "const unsigned char ";
            *out << "tag_default_config[" << output.length() << "] = {\n   ";
            int i;
            for (i = 0; i < output.length(); i++) {
                *out << (unsigned int) (output[i] & 0xff);
                if (i+1 < output.length()) {
                    *out << ", ";
                    if ((i % 10 ) == 9) {
                        *out << "\n   ";
                    }
                }
            }
            *out << "\n};\n";
            *out << "const unsigned int tag_default_config_len = " << output.length() << ";\n";
        } else {
            cerr << err << std::endl;
        }
        return 0;

    }

}
