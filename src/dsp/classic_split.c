#include "classic_split.h"
static float round_difference(float difference) {
    volatile float rounded=difference+9.999999747378752e-05f;
    return rounded+-9.999999747378752e-05f;
}
static void split_channel(rs_classic_split_state *s, unsigned c, const float input[4],
                          float low[4], float middle[4], float high[4]) {
    float low_history=s->history[c][0], middle_history=s->history[c][1];
    for (unsigned i=0;i<4;++i) {
        low_history=round_difference(input[i]-low_history)*s->low_coefficient+low_history;
        float rest=input[i]-low_history;
        middle_history=round_difference(rest-middle_history)*s->middle_coefficient+middle_history;
        low[i]=low_history;middle[i]=middle_history;high[i]=rest-middle_history;
    }
    s->history[c][0]=low_history;s->history[c][1]=middle_history;
}
void rs_classic_split_sample(rs_classic_split_state *s, float left, float right,
                             float low[2], float middle[2], float high[2]) {
    float input[2]={left,right};
    for (unsigned c=0;c<2;++c) {
        float l=round_difference(input[c]-s->history[c][0])*s->low_coefficient+s->history[c][0];
        float rest=input[c]-l;
        float m=round_difference(rest-s->history[c][1])*s->middle_coefficient+s->history[c][1];
        s->history[c][0]=l;s->history[c][1]=m;
        low[c]=l;middle[c]=m;high[c]=rest-m;
    }
}
void rs_classic_split(rs_classic_split_state *s, const float left[4],
                      const float right[4], rs_classic_split_bands *out) {
    split_channel(s,0,left,out->low[0],out->middle[0],out->high[0]);
    split_channel(s,1,right,out->low[1],out->middle[1],out->high[1]);
}
void rs_classic_split_mono(rs_classic_split_state *s, const float input[4],
                           float low[4], float middle[4], float high[4]) {
    split_channel(s,0,input,low,middle,high);
}
