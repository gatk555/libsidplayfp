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

#ifndef SWINSIDSIM_EMU_H
#define SWINSIDSIM_EMU_H

#include <stdint.h>

#include "sidemu.h"
#include "Event.h"

#include "sidcxx11.h"

#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/sim_gdb.h>
#include <simavr/sim_vcd_file.h>
#include <simavr/avr_ioport.h>

class sidbuilder;

namespace libsidplayfp
{

class SwinSIDError
{
private:
    const char* message;

public:
    SwinSIDError(const char* msg) :
        message(msg) {}
    const char* getMessage() const { return message; }
};

class SwinSIDsim final : public sidemu
{
private:
    elf_firmware_t &swinsid_fw;
    avr_t *swinsid_avrsim;
    int16_t m_sample;
    bool m_sample_generated;
    uint_least32_t m_sample_rate;
    avr_int_vector_t *m_int0_interrupt_vector;
    int m_sync_avr_c64_clock;

    static void ocr1bl_write_notify(struct avr_irq_t * irq, uint32_t value, void * param);

public:
    static const char* getCredits();

public:
    SwinSIDsim(sidbuilder *builder, const std::string &fw_filename);
    ~SwinSIDsim();

    bool getStatus() const { return m_status; }
    uint_least32_t getSampleRate() const { return m_sample_rate; }

    uint8_t read(uint_least8_t addr) override;
    void write(uint_least8_t addr, uint8_t data) override;

    // c64sid functions
    void reset(uint8_t volume) override;

    // Standard SID emu functions
    void clock() override;

    void sampling(float systemclock, float freq,
        SidConfig::sampling_method_t method, bool) override;

    void voice(unsigned int num, bool mute) override;

    void model(SidConfig::sid_model_t model, bool digiboost) override;

};

}

#endif // SWINSIDSIM_EMU_H
