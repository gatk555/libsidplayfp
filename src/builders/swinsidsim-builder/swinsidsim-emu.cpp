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

void SwinSIDsim::ocr1bl_write_notify(struct avr_irq_t * irq, uint32_t value, void * param) {
    libsidplayfp::SwinSIDsim *sidemu = (libsidplayfp::SwinSIDsim *) param;
    sidemu->m_sample = ((sidemu->swinsid_avrsim->data[OCR1AL] << 8) | sidemu->swinsid_avrsim->data[OCR1BL]) - 32768;
    sidemu->m_buffer[sidemu->m_bufferpos]=sidemu->m_sample;
    sidemu->m_bufferpos++;

    sidemu->m_sample_generated = true;
//<>printf("OCR1BL written, sample %d\n",sample);
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

SwinSIDsim::SwinSIDsim(sidbuilder *builder, const std::string &fw_filename) :
    swinsid_fw(*new elf_firmware_t),
    swinsid_avrsim(NULL),
    sidemu(builder),
    m_sync_avr_c64_clock(0)
{
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
//    reset(0);

    avr_irq_t *ocr1bl_irq;
    ocr1bl_irq=avr_iomem_getirq(swinsid_avrsim,OCR1BL,NULL,AVR_IOMEM_IRQ_ALL);
    avr_irq_register_notify(ocr1bl_irq,&ocr1bl_write_notify,this);

//    cout << "Determine samplerate\n";
    /* Wait for a sample */
    int state = cpu_Running;
    while ((state != cpu_Done) && (state != cpu_Crashed) && !m_sample_generated)
        state = avr_run(swinsid_avrsim);
    m_sample_generated = false;

    /* We need to wait for another sample before we start the measurement, because the first timer
       cycle, it is influenced by the initialized value inside the timer register, which results in
       one AVR cycle less. */
    while ((state != cpu_Done) && (state != cpu_Crashed) && !m_sample_generated)
        state = avr_run(swinsid_avrsim);

    /* Now we can start the measurement */
    avr_cycle_count_t cycles = swinsid_avrsim->cycle;
    m_sample_generated = false;
    while ((state != cpu_Done) && (state != cpu_Crashed) && !m_sample_generated)
        state = avr_run(swinsid_avrsim);
    cycles = swinsid_avrsim->cycle - cycles;
    m_sample_rate = swinsid_avrsim->frequency / cycles;
//    cout << "SwinSID samplerate: " << m_sample_rate << endl;

    m_sample_generated = false;

    /* Find the external interrupt INT0 vector in the table of interrupt vectors */
    for (int i=0; i<swinsid_avrsim->interrupts.vector_count; i++ ) {
      if (swinsid_avrsim->interrupts.vector[i]->vector == 1)
        m_int0_interrupt_vector = swinsid_avrsim->interrupts.vector[i];
    }
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
}

void SwinSIDsim::write(uint_least8_t addr, uint8_t data)
{
    avr_irq_t *pin_irq;

    clock();

#if 0
    /* This sets the IO pins of the SwinSID using the irq mechanism in
       SimAVR. It is the cleanest way to do it, since using the irq
       mechanism, all on-chip AVR pheripherals can react to the pin changes.
       Unfortunately, doing it this way, is also very slow.

       Therefore this method is not used.
     */

    /* Output the register address on pin PC0..PC4 of the Atmega88 */
    pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('C'),0);
    avr_raise_irq(pin_irq,addr & 1 ? 1 : 0);
    pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('C'),1);
    avr_raise_irq(pin_irq,addr & 2 ? 1 : 0);
    pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('C'),2);
    avr_raise_irq(pin_irq,addr & 4 ? 1 : 0);
    pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('C'),3);
    avr_raise_irq(pin_irq,addr & 8 ? 1 : 0);
    pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('C'),4);
    avr_raise_irq(pin_irq,addr & 16 ? 1 : 0);

    /* Output the data on pin PD0, PD1, PC5, PD3..PD7 of the Atmega88 */
    pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('D'),0);
    avr_raise_irq(pin_irq,data & 1 ? 1 : 0);
    pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('D'),1);
    avr_raise_irq(pin_irq,data & 2 ? 1 : 0);
    pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('C'),5);
    avr_raise_irq(pin_irq,data & 4 ? 1 : 0);
    pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('D'),3);
    avr_raise_irq(pin_irq,data & 8 ? 1 : 0);
    pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('D'),4);
    avr_raise_irq(pin_irq,data & 16 ? 1 : 0);
    pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('D'),5);
    avr_raise_irq(pin_irq,data & 32 ? 1 : 0);
    pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('D'),6);
    avr_raise_irq(pin_irq,data & 64 ? 1 : 0);
    pin_irq=avr_io_getirq(swinsid_avrsim,AVR_IOCTL_IOPORT_GETIRQ('D'),7);
    avr_raise_irq(pin_irq,data & 128 ? 1 : 0);
#endif

#if 1
    /* Rather than using the irq mechanism, we can also set the state of the
       pins by writing directly to AVR memory. By writing directly to AVR
       memory, SimAVR cannot make any of the on-chip pheripherals to
       react on a pin-change, however, since the SwinSID just reads the pin
       states from memory, it is also kind of irrelevant for the SwinSID.

       Since writing to AVR memory is way faster than the irq mechanism,
       makes the simulation fast enough for realtime playback to soundcards,
       it is the preferred method.
     */
    uint8_t portc = addr | ((data & 4) << 3);  /* Move D2 to PC5 */
    uint8_t portd = data | 4;                  /* CS line on PD2 always high */
    avr_core_watch_write(swinsid_avrsim, PINC, portc);
    avr_core_watch_write(swinsid_avrsim, PIND, portd);
#endif

    /* Trigger a chip-select interrupt on the SwinSID.
       The SID CS line is connected to external interrupt 0 on the Atmega88,
       which uses interrupt vector 1. */
    avr_raise_interrupt(swinsid_avrsim, m_int0_interrupt_vector);
}

void SwinSIDsim::clock()
{
    int start;
    const event_clock_t cycles = eventScheduler->getTime(EVENT_CLOCK_PHI1) - m_accessClk;
    m_accessClk += cycles;
    start = m_bufferpos;
    avr_cycle_count_t avrcycles = swinsid_avrsim->cycle;
    for (int i=0;i<cycles;i++) {
      avr_cycle_count_t avrcycles = swinsid_avrsim->cycle;
      while (m_sync_avr_c64_clock<32000) {
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
