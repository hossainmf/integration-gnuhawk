/*
 * This file is protected by Copyright. Please refer to the COPYRIGHT file
 * distributed with this source distribution.
 * 
 * This file is part of GNUHAWK.
 * 
 * GNUHAWK is free software: you can redistribute it and/or modify is under the 
 * terms of the GNU General Public License as published by the Free Software 
 * Foundation, either version 3 of the License, or (at your option) any later 
 * version.
 * 
 * GNUHAWK is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS 
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more 
 * details.
 * 
 * You should have received a copy of the GNU General Public License along with 
 * this program.  If not, see http://www.gnu.org/licenses/.
 */

#ifndef FREQ_XLATING_FIR_FILTER_CCF_IMPL_H
#define FREQ_XLATING_FIR_FILTER_CCF_IMPL_H

#include "freq_xlating_fir_filter_ccf_base.h"

class freq_xlating_fir_filter_ccf_i : public freq_xlating_fir_filter_ccf_base
{
    public:
        freq_xlating_fir_filter_ccf_i(const char *uuid, const char *label);
        ~freq_xlating_fir_filter_ccf_i();

        //
        // createBlock
        //
        // Create the actual GNU Radio Block to that will perform the work method. The resulting
        // block object is assigned to gr_stpr
        //
        // Add property change callbacks for getter/setter methods
        //
        //
        void createBlock();

        //
        // createOutputSRI
        //
        // Called by setupIOMappings when an output mapping is defined. For each output mapping
        // defined, a call to createOutputSRI will be issued with the associated output index.
        // This default SRI and StreamID will be saved to the mapping and pushed down stream via pushSRI.
        // The subclass is responsible for overriding behavior of this method. The index provide matches
        // the stream index number that will be use by the GR Block object
        //
        // @param idx : output stream index number to associate the returned SRI object with
        // @return sri : default SRI object passed down stream over a RedHawk port
        //      
        BULKIO::StreamSRI  createOutputSRI( int32_t idx );
        void set_taps(const std::string &id);
        std::vector<float> get_taps();
};

#endif
