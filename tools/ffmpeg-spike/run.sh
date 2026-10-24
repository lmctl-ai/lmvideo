#!/usr/bin/env bash
# Reproduces the in-process FFmpeg feasibility spike (durable-memory/06-ffmpeg-in-process-feasibility.md).
# Builds nasm, libvpx, OpenH264 and an LGPL libav* subset as static libraries under $SPIKE_DIR (default ./ffspike), links enc.c
# against them and encodes VP9/WebM, H.264/MP4, MPEG-4/MP4 and APNG from RGBA frames. Needs gcc, make, git, curl, pkg-config.
# Spike: static LGPL FFmpeg (libav* only) + libvpx + OpenH264, linked into a tiny encoder; measure everything.
set -euo pipefail
W="${SPIKE_DIR:-$PWD/ffspike}"; P=$W/prefix; HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; mkdir -p $P $W/src; mkdir -p $W; cd $W/src
export PATH=$P/bin:$PATH PKG_CONFIG_PATH=$P/lib/pkgconfig
t0=$(date +%s); stamp(){ echo "[$(( $(date +%s)-t0 ))s] $*"; }
stamp nasm; [ -x $P/bin/nasm ] || { curl -sL https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/nasm-2.16.03.tar.xz | tar xJ; (cd nasm-2.16.03 && ./configure --prefix=$P >/dev/null && make -j4 >/dev/null && make install >/dev/null); }
stamp libvpx; [ -f $P/lib/libvpx.a ] || { [ -d libvpx ] || git clone -q --depth 1 -b ${VPX_TAG:-v1.15.0} https://github.com/webmproject/libvpx; (cd libvpx && ./configure --prefix=$P --disable-examples --disable-tools --disable-docs --disable-unit-tests --enable-vp9 --enable-vp8 --enable-pic --enable-static --disable-shared --enable-runtime-cpu-detect >/dev/null && make -j4 >/dev/null && make install >/dev/null); }
stamp openh264; [ -f $P/lib/libopenh264.a ] || { [ -d openh264 ] || git clone -q --depth 1 -b v2.6.0 https://github.com/cisco/openh264; (cd openh264 && make -j4 PREFIX=$P BUILDTYPE=Release install-static >/dev/null 2>&1); }
ls $P/lib | head -20
stamp ffmpeg-configure; [ -d ffmpeg ] || git clone -q --depth 1 -b n7.1.5 https://github.com/FFmpeg/FFmpeg ffmpeg
cd ffmpeg
[ -f config.h ] || ./configure --prefix=$P --enable-static --disable-shared --enable-pic --disable-programs --disable-doc --disable-debug --disable-autodetect --enable-zlib \
  --disable-everything --enable-libvpx --enable-libopenh264 \
  --enable-encoder=libvpx_vp9,libvpx_vp8,libopenh264,mpeg4,gif,apng,png,mjpeg \
  --enable-decoder=h264,vp9,vp8,mpeg4,gif,png,apng,mjpeg,rawvideo,aac,mp3,opus,vorbis,flac,pcm_s16le \
  --enable-muxer=mp4,webm,gif,apng,mov,image2,null --enable-demuxer=mov,matroska,gif,apng,image2,wav,mp3,ogg,flac,aac \
  --enable-parser=h264,vp9,vp8,mpeg4video,aac,png --enable-protocol=file \
  --enable-swscale --enable-swresample --enable-avfilter \
  --enable-filter=scale,format,fps,null,xfade,fade,overlay,concat,trim,setpts,anull,aresample,amix,volume \
  --extra-cflags="-I$P/include" --extra-ldflags="-L$P/lib" --pkg-config-flags=--static >$W/ffconf.log 2>&1 || { tail -30 $W/ffconf.log; exit 1; }
grep -E "^License|License:" $W/ffconf.log ffbuild/config.mak 2>/dev/null | head -3
stamp ffmpeg-make; make -j4 >$W/ffmake.log 2>&1 && make install >/dev/null 2>&1
stamp ffmpeg-done; ls -la $P/lib/libav*.a $P/lib/libsw*.a
cd $W
cp "$HERE/enc.c" enc.c
stamp link
gcc -O2 enc.c -o enc_static -I$P/include -L$P/lib $(pkg-config --static --cflags --libs libavformat libavcodec libswscale libavutil libswresample) -lm -lpthread -lstdc++ 2>&1 | tail -5
ls -la enc_static; ldd enc_static | head -8
stamp run; time ./enc_static; ls -la out_*
for f in out_*; do echo "== $f"; ffprobe -v error -show_entries stream=codec_name,width,height,nb_frames -of compact $f 2>&1 | head -2; done
stamp spike-complete
