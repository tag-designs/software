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
        cerr << "Usage: " << argv[0] << " json-file" << endl;
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
            string output;
            configin.SerializeToString(&output);
            //cout << "// generated output len : " << output.length() << endl;
            cout << "const unsigned char ";
            cout << "tag_default_config[" << output.length() << "] = {\n   ";
            int i;
            for (i = 0; i < output.length(); i++) {
                cout << (unsigned int) (output[i] & 0xff);
                if (i+1 < output.length()) {
                    cout << ", ";
                    if ((i % 10 ) == 9) {
                        cout << "\n   ";
                    }
                }
            }
            cout << "\n};\n";
            cout << "const unsigned int tag_default_config_len = " << output.length() << ";\n";
        } else {
            cerr << err << std::endl;
        }
        return 0;

    }

}