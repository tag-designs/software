
class Ema {

public:

    Ema(float a=0.3):alpha(a){};
    float operator+(float v) { value = alpha*v + (1-alpha)*value; return value; };
    float val(){ return value;};

private:    
    float value = 0.0;
    const float alpha;
};