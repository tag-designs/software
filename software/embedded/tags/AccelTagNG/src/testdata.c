#include <stdio.h>
#include <math.h>
#include <stdint.h>

int main(){
    int i;
    int16_t x,y,z;
    fprintf(stdout, "static const int16_t testdata[] = {");
    for (i =0; i < 50; i++){
        if (i%4 == 0)
            fprintf(stdout,"\n");
        x = (int) (sin(2*M_PI * i/25)*8000);
        y = (int) (sin(2*M_PI * (i/25.0+ 1/3.0))*8000);
        z = (int) (sin(2*M_PI * (i/25.0 + 2/3.0))*8000);
        fprintf(stdout, "%4d, %4d, %4d%s", x, y, z, i<49 ? ", " : " ");
    }
    fprintf(stdout,"\n};\n");
}