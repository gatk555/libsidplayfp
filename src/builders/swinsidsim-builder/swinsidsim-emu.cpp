/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2019 Leandro Nini <drfiemost@users.sourceforge.net>
 * Copyright 2007-2010 Antti Lankila
 * Copyright 2001 Simon White
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "swinsidsim-emu.h"

#include <sstream>
#include <string>
#include <algorithm>

#include <iostream>
using std::cout, std::endl;

#include "sidplayfp/siddefs.h"

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#define PINC   0x26
#define PIND   0x29

#define OCR1AL 0x88
#define OCR1BL 0x8a


/*
 * In order to understand the code below, please note that:
 * - An interrupt is a CPU interrupt on the simulated AVR processor
 * - An irq is a mechanism within SimAVR to send messages to components of the simulator.
 *   For example: We can communicate changes on the pins of the Atmega88 to the code that simulates
 *   these pins, by raising pin-charge irqs. This does not necessarily trigger any pin-change
 *   interrupt on the simulated AVR (the SwinSID doesn't use pin-change interrupts at all), but
 *   the pin-change irq sent to the simulator will make the simulated SwinSID read the changed values
 *   on the pins.
 */

namespace libsidplayfp
{
static void avr_sleep(avr_t *avr, unsigned long int how_long);

void SwinSIDsim::ocr1bl_write_notify(struct avr_irq_t * irq, uint32_t value, void * param) {
    libsidplayfp::SwinSIDsim *sidemu = (libsidplayfp::SwinSIDsim *) param;
    sidemu->m_sample = ((sidemu->swinsid_avrsim->data[OCR1AL] << 8) | sidemu->swinsid_avrsim->data[OCR1BL]) - 32768;
    sidemu->m_buffer[sidemu->m_bufferpos]=sidemu->m_sample;
    sidemu->m_bufferpos++;

    sidemu->m_sample_generated = true;
//    printf("OCR1BL written, sample %d\n", sidemu->m_sample);
//    output_buffer[output_idx]=sample;
//    output_idx++;
//    if (output_idx==4096) {
//        output_idx=0;
//        fwrite(output_buffer,sizeof(output_buffer),1,wav_file);
//    }
//    sample_counter--;
//    if (sample_counter == 0)
//        simulation_ready=1;
}



const char* SwinSIDsim::getCredits()
{
    static std::string credits;

    if (credits.empty())
    {
        // Setup credits
        std::ostringstream ss;
        ss << "SwinSIDsim V" << VERSION << ":\n";
        ss << "\t(C) 2023 Daniel Mantione\n";
        credits = ss.str();
    }

    return credits.c_str();
}

void SwinSIDsim::wait_for_sample(void)
{
    int state;

    do {
        state = avr_run(swinsid_avrsim);
    } while ((state != cpu_Done) && (state != cpu_Crashed) &&
	     !m_sample_generated);
    m_sample_generated = false;
}

SwinSIDsim::SwinSIDsim(sidbuilder *builder, const std::string &fw_filename) :
    swinsid_fw(*new elf_firmware_t),
    swinsid_avrsim(NULL),
    sidemu(builder),
    m_sync_avr_c64_clock(0)
{
    avr_irq_t         *ocr1bl_irq;
    avr_cycle_count_t  cycles;

    m_buffer = new short[OUTPUTBUFFERSIZE];
    m_sample = 0;
    m_sample_generated = false;

    cout << "SwinSID loading firmware\n";

    swinsid_fw = {{0}};
    elf_read_firmware(fw_filename.c_str(), &swinsid_fw);

    cout << "Creating MCU\n";

    swinsid_avrsim = avr_make_mcu_by_name(swinsid_fw.mmcu);
    if (!swinsid_avrsim) {
        static char msg[256];
        snprintf(msg,256,"AVR '%s' not known",swinsid_fw.mmcu);
        throw SwinSIDError(msg);
    }
    avr_init(swinsid_avrsim);
    avr_load_firmware(swinsid_avrsim, &swinsid_fw);
    portc_irq = avr_io_getirq(swinsid_avrsim,
			      AVR_IOCTL_IOPORT_GETIRQ('C'),
			      IOPORT_IRQ_PIN_ALL_IN);
    portd_irq = avr_io_getirq(swinsid_avrsim,
			      AVR_IOCTL_IOPORT_GETIRQ('D'),
			      IOPORT_IRQ_PIN_ALL_IN);

    ocr1bl_irq = avr_iomem_getirq(swinsid_avrsim,
				  OCR1BL, NULL, AVR_IOMEM_IRQ_ALL);
    avr_irq_register_notify(ocr1bl_irq, &ocr1bl_write_notify, this);

    /* Increase the instruction burst. */

    swinsid_avrsim->run_cycle_limit = 100;
    swinsid_avrsim->sleep = avr_sleep;

    /* Determine samplerate. */

    wait_for_sample();

    /* We need to wait for another sample before we start the measurement,
     * because the first timer cycle is influenced by the initialized value
     * inside the timer register, which results in one AVR cycle less.
     */

    wait_for_sample();

    /* Now we can start the measurement */

    cycles = swinsid_avrsim->cycle;
    m_sample_generated = false;
    wait_for_sample();
    cycles = swinsid_avrsim->cycle - cycles;
    m_sample_rate = swinsid_avrsim->frequency / cycles;
    cout << "SwinSID samplerate: " << m_sample_rate << endl;

    m_sample_generated = false;
}

SwinSIDsim::~SwinSIDsim()
{
    avr_terminate(swinsid_avrsim);
    delete swinsid_avrsim;
    delete &swinsid_fw;
    delete[] m_buffer;
}

// Standard component options
void SwinSIDsim::reset(uint8_t volume)
{
    m_accessClk = 0;
    avr_reset(swinsid_avrsim);
//    m_sid.write(0x18, volume);
}

uint8_t SwinSIDsim::read(uint_least8_t addr)
{
    clock();
//    return m_sid.read(addr);
    return 0;
}

void SwinSIDsim::write(uint_least8_t addr, uint8_t data)
{
    avr_irq_t *pin_irq;
    uint8_t portc, portd;

    clock();
    portc = addr | ((data & 4) << 3);  /* Move D2 to PC5 */
    avr_raise_irq(portc_irq, portc);
    portd = data & 0xfb;               /* CS line on PD2 goes low */
    avr_raise_irq(portd_irq, portd);
    portd = data | 4;                  /* CS line on PD2 goes high */
    avr_raise_irq(portd_irq, portd);
}

static void avr_sleep(avr_t *avr, unsigned long int how_long)
{
#if 0
  static unsigned long int total_sleep;
  static unsigned int     calls;

  total_sleep += how_long;
  if (++calls >= 500000) {
    calls = 0;
    printf("AVR sleeping %lu totals %lu/%lu: %lu\%\n",
	   how_long, total_sleep, avr->cycle,
	   (total_sleep *100) / (total_sleep + avr->cycle));
  }
#endif
}

void SwinSIDsim::clock()
{
    event_clock_t     cycles;
    avr_cycle_count_t avrcycles;

    cycles = eventScheduler->getTime(EVENT_CLOCK_PHI1) - m_accessClk;
    m_accessClk += cycles;

    for (int i = 0; i < cycles; i++) {
      avrcycles = swinsid_avrsim->cycle;
      while (m_sync_avr_c64_clock < 32000) {
        int state = avr_run(swinsid_avrsim);
        if (state == cpu_Done || state == cpu_Crashed)
          break;
        m_sync_avr_c64_clock += 985 * (swinsid_avrsim->cycle - avrcycles);
        avrcycles = swinsid_avrsim->cycle;
      }
      m_sync_avr_c64_clock -= 32000;
    }
}

void SwinSIDsim::sampling(float systemclock, float freq,
        SidConfig::sampling_method_t method, bool)
{
//    reSIDfp::SamplingMethod sampleMethod;
    switch (method)
    {
    case SidConfig::INTERPOLATE:
//        sampleMethod = reSIDfp::DECIMATE;
        break;
//    case SidConfig::RESAMPLE_INTERPOLATE:
//        sampleMethod = reSIDfp::RESAMPLE;
//        break;
    default:
        m_status = false;
        m_error = ERR_INVALID_SAMPLING;
        return;
    }

//    try
//    {
//        const int halfFreq = (freq > 44000) ? 20000 : 9 * freq / 20;
//        m_sid.setSamplingParameters(systemclock, sampleMethod, freq, halfFreq);
//    }
//    catch (reSIDfp::SIDError const &)
//    {
//        m_status = false;
//        m_error = ERR_UNSUPPORTED_FREQ;
//        return;
//    }

    m_status = true;
}

void SwinSIDsim::voice(unsigned int num, bool mute) {
    m_status = false;
    m_error = "SwinSID does not support voice muting";
}

// Set the emulated SID model
void SwinSIDsim::model(SidConfig::sid_model_t model, bool digiboost)
{
    avr_irq_t *pin_irq;

    switch (model)
    {
        case SidConfig::MOS6581:
            pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('B'),0);
            avr_raise_irq(pin_irq,0);
            break;
        case SidConfig::MOS8580:
            pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('B'),0);
            avr_raise_irq(pin_irq,1);
            break;
        default:
            m_status = false;
            m_error = ERR_INVALID_CHIP;
            return;
    }

    m_status = true;
}

}
