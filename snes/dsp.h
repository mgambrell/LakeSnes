#pragma once

#include <stdint.h>

namespace LakeSnes
{
	struct StateHandler;

	struct DspChannel {
		// pitch
		uint16_t pitch;
		uint16_t pitchCounter;
		bool pitchModulation;
		// brr decoding
		int16_t decodeBuffer[12];
		uint8_t bufferOffset;
		uint8_t srcn;
		uint16_t decodeOffset;
		uint8_t blockOffset; // offset within brr block
		uint8_t brrHeader;
		bool useNoise;
		uint8_t startDelay;
		// adsr, envelope, gain
		uint8_t adsrRates[4]; // attack, decay, sustain, gain
		uint8_t adsrState; // 0: attack, 1: decay, 2: sustain, 3: release
		uint8_t sustainLevel;
		uint8_t gainSustainLevel;
		bool useGain;
		uint8_t gainMode;
		bool directGain;
		uint16_t gainValue; // for direct gain
		uint16_t preclampGain; // for bent increase
		uint16_t gain;
		// keyon/off
		bool keyOn;
		bool keyOff;
		// output
		int16_t sampleOut; // final sample, to be multiplied by channel volume
		int8_t volumeL;
		int8_t volumeR;
		bool echoEnable;
	};

	struct Dsp {
		// mirror ram
		uint8_t ram[0x80];
		// 8 channels
		DspChannel channel[8];
		// overarching
		uint16_t counter;
		uint16_t dirPage;
		bool evenCycle;
		bool mute;
		bool reset;
		int8_t masterVolumeL;
		int8_t masterVolumeR;
		// accumulation
		int16_t sampleOutL;
		int16_t sampleOutR;
		int16_t echoOutL;
		int16_t echoOutR;
		// noise
		int16_t noiseSample;
		uint8_t noiseRate;
		// echo
		bool echoWrites;
		int8_t echoVolumeL;
		int8_t echoVolumeR;
		int8_t feedbackVolume;
		uint16_t echoBufferAdr;
		uint16_t echoDelay;
		uint16_t echoLength;
		uint16_t echoBufferIndex;
		uint8_t firBufferIndex;
		int8_t firValues[8];
		int16_t firBufferL[8];
		int16_t firBufferR[8];
		// sample ring buffer (2048 samples, *2 for stereo)
		int16_t sampleBuffer[0x800 * 2];
		uint16_t sampleOffset; // current offset in samplebuffer
		uint32_t lastFrameBoundary;
	};

	void dsp_init();
	void dsp_free();
	void dsp_reset();
	void dsp_handleState(StateHandler* sh);
	void dsp_cycle();
	uint8_t dsp_read(uint8_t adr);
	void dsp_write(uint8_t adr, uint8_t val);
	void dsp_getSamples(int16_t* sampleData, int samplesPerFrame);
	void dsp_newFrame();


}