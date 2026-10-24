#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
static int encode(const char* path,const char* fmt,const char* codec_name,int w,int h,int n,int fps,int gif){
  AVFormatContext* oc=NULL; avformat_alloc_output_context2(&oc,NULL,fmt,path); if(!oc){fprintf(stderr,"no muxer %s\n",fmt);return 1;}
  const AVCodec* c=avcodec_find_encoder_by_name(codec_name); if(!c){fprintf(stderr,"no encoder %s\n",codec_name);return 2;}
  AVStream* st=avformat_new_stream(oc,NULL); AVCodecContext* cc=avcodec_alloc_context3(c);
  cc->width=w;cc->height=h;cc->time_base=(AVRational){1,fps};cc->framerate=(AVRational){fps,1};cc->gop_size=fps;
  cc->pix_fmt=gif?AV_PIX_FMT_PAL8:(c->pix_fmts?c->pix_fmts[0]:AV_PIX_FMT_YUV420P);
  if(!strcmp(codec_name,"libvpx-vp9")||!strcmp(codec_name,"libvpx-vp8")){cc->pix_fmt=AV_PIX_FMT_YUV420P;av_opt_set(cc->priv_data,"deadline","good",0);av_opt_set(cc->priv_data,"cpu-used","4",0);cc->bit_rate=600000;}
  if(!strcmp(codec_name,"libopenh264")){cc->pix_fmt=AV_PIX_FMT_YUV420P;cc->bit_rate=900000;}
  if(!strcmp(codec_name,"mpeg4")){cc->pix_fmt=AV_PIX_FMT_YUV420P;cc->bit_rate=900000;}
  if(oc->oformat->flags&AVFMT_GLOBALHEADER)cc->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
  if(avcodec_open2(cc,c,NULL)<0){fprintf(stderr,"open %s failed\n",codec_name);return 3;}
  avcodec_parameters_from_context(st->codecpar,cc); st->time_base=cc->time_base;
  if(avio_open(&oc->pb,path,AVIO_FLAG_WRITE)<0)return 4; avformat_write_header(oc,NULL);
  AVFrame* fr=av_frame_alloc(); fr->format=cc->pix_fmt;fr->width=w;fr->height=h; av_frame_get_buffer(fr,0);
  struct SwsContext* sws=sws_getContext(w,h,AV_PIX_FMT_RGBA,w,h,cc->pix_fmt==AV_PIX_FMT_PAL8?AV_PIX_FMT_RGB8:cc->pix_fmt,SWS_BILINEAR,NULL,NULL,NULL);
  uint8_t* rgba=malloc((size_t)w*h*4); AVPacket* pk=av_packet_alloc();
  for(int i=0;i<=n;i++){
    if(i<n){ for(int y=0;y<h;y++)for(int x=0;x<w;x++){uint8_t* p=rgba+((size_t)y*w+x)*4; double t=(double)i/n; p[0]=(uint8_t)(127+127*sin(x*0.05+t*6.28)); p[1]=(uint8_t)(127+127*sin(y*0.07-t*6.28)); p[2]=(uint8_t)(255*t); p[3]=255;}
      av_frame_make_writable(fr); const uint8_t* src[1]={rgba}; int ls[1]={w*4}; sws_scale(sws,src,ls,0,h,fr->data,fr->linesize); fr->pts=i; avcodec_send_frame(cc,fr);} else avcodec_send_frame(cc,NULL);
    while(avcodec_receive_packet(cc,pk)==0){ av_packet_rescale_ts(pk,cc->time_base,st->time_base); pk->stream_index=st->index; av_interleaved_write_frame(oc,pk); av_packet_unref(pk);} }
  av_write_trailer(oc); avio_closep(&oc->pb); return 0; }
int main(int argc,char**argv){ av_log_set_level(AV_LOG_ERROR);
  int r=0; r|=encode("out_vp9.webm","webm","libvpx-vp9",320,180,120,30,0);
  r|=encode("out_h264.mp4","mp4","libopenh264",320,180,120,30,0);
  r|=encode("out_mpeg4.mp4","mp4","mpeg4",320,180,120,30,0);
  r|=encode("out_apng.apng","apng","apng",320,180,30,15,0);
  printf("done r=%d\n",r); return r; }
