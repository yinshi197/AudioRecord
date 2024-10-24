#include <iostream>
#include "opus.h"
using namespace std;

#define MAX_PACKET_SIZE     4000
void Encoder(OpusEncoder *encoder, opus_int16 *input_data, int nb_sample, FILE *out)
{   
    opus_int32 ret = 0;
    unsigned char *out_data;
    ret = opus_encode(encoder, input_data, nb_sample, out_data, MAX_PACKET_SIZE); 
}

//opus编码器的基本框架，不能直接运行。
int main()
{
    int err;
    /*
        OPUS_APPLICATION_VOIP：对语音信号进行处理，适用于voip 业务场景

        OPUS_APPLICATION_AUDIO：这个模式适用于音乐类型等非语音内容

        OPUS_APPLICATION_RESTRICTED_LOWDELAY：低延迟模式
    */
    int applications[3] = { OPUS_APPLICATION_AUDIO, OPUS_APPLICATION_VOIP, OPUS_APPLICATION_RESTRICTED_LOWDELAY };
    OpusEncoder *encoder = nullptr;

    //1.初始化编码器
    encoder = opus_encoder_create(48000, 2, applications[0], &err);
    if(err != OPUS_OK || encoder == nullptr)
    {
        cout << "create opus encoder to failed\n";
        exit(-1);
    }

    //2.设置编码器参数,第二个参数可以设置编码相关的参数或者通用参数
    opus_encoder_ctl(encoder, OPUS_SET_VBR(0)); //0:CBR, 1:VBR(可变比特率 )
    //下面三个最常用
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(64000)); //设置比特率，单位bit/s
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(8));  //配置编码器的计算复杂度。支持的范围是 0-10（包括 0-10），其中 10 表示最高复杂性。
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));  //配置正在编码的信号类型。给编码器提供参考
    
    opus_encoder_ctl(encoder, OPUS_SET_LSB_DEPTH(16));  //位深(采样大小)，每个采样16个bit，2个byte。给编码器提供参考

    //3.编码
    //Encoder()

    //4.释放资源
    if(encoder)
    {
        opus_encoder_destroy(encoder);
    }

    system("pause");
    return 0;
}

