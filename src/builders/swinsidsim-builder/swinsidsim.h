/*
 * This file is part of libsidplayfp, a SID player engine.
 *
 * Copyright 2011-2013 Leandro Nini <drfiemost@users.sourceforge.net>
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

#ifndef SWINSIDSIM_H
#define SWINSIDSIM_H

#include "sidplayfp/sidbuilder.h"
#include "sidplayfp/siddefs.h"

/**
 * SwinSIDsim Builder Class
 */
class SID_EXTERN SwinSIDsimBuilder: public sidbuilder
{
private:
	const std::string &m_fw_filename;
public:
    SwinSIDsimBuilder(const char * const name, const std::string &fw_filename);
    ~SwinSIDsimBuilder();

    /**
     * Available sids.
     *
     * @return the number of available sids, 0 = endless.
     */
    unsigned int availDevices() const { return 0; }

    /**
     * Create the sid emu.
     *
     * @param sids the number of required sid emu
     */
    unsigned int create(unsigned int sids);

    /**
     * Get the sample rate of the SwinSID. All SwinSIDs share the same firmware so will use
     * the same sample rate. create must have been called before getSampleRate can be called.
     */
    uint_least32_t getSampleRate() const;

    /**
     * enable/disable filter.
     */
    void filter(bool enable) {};

    const char *credits() const;

};

#endif // SWINSIDSIM_H
