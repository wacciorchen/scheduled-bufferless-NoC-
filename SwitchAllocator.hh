/*
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Niket Agarwal
 *          Tushar Krishna
 */


#ifndef __MEM_RUBY_NETWORK_GARNET2_0_SWITCHALLOCATOR_HH__
#define __MEM_RUBY_NETWORK_GARNET2_0_SWITCHALLOCATOR_HH__

#include <iostream>
#include <vector>
#include <map>

#include "mem/ruby/common/Consumer.hh"
#include "mem/ruby/network/garnet2.0/CommonTypes.hh"
#include "mem/ruby/network/garnet2.0/GarnetNetwork.hh"
#include "mem/ruby/network/garnet2.0/InputUnit.hh"
#include "mem/ruby/network/garnet2.0/OutputUnit.hh"
#include "mem/ruby/network/garnet2.0/Router.hh"
#include "mem/ruby/network/garnet2.0/flitBuffer.hh"

using namespace std;
class Router;
class InputUnit;
class OutputUnit;

class SwitchAllocator : public Consumer
{
  public:
    SwitchAllocator(Router *router);
    void wakeup();
    void init();
    void clear_request_vector();
    void check_for_wakeup();
    int get_vnet (int invc);
    void print(std::ostream& out) const {};
    void arbitrate_inports();
    void arbitrate_outports();
    bool send_allowed(int inport, int invc, int outport, int outvc);
    int vc_allocate(int outport, int inport, int invc);

    //added for deflection
    void permutation_CHIPPER();
    void permutation_BLESS();

    void verify_VCs_empty();

    inline double
    get_input_arbiter_activity()
    {
        return m_input_arbiter_activity;
    }
    inline double
    get_output_arbiter_activity()
    {
        return m_output_arbiter_activity;
    }

    void resetStats();

  private:
    int m_num_inports, m_num_outports;
    int m_num_vcs, m_vc_per_vnet;

    double m_input_arbiter_activity, m_output_arbiter_activity;

    Router *m_router;
    std::vector<int> m_round_robin_invc;
    std::vector<int> m_round_robin_inport;
    std::vector<std::vector<bool>> m_port_requests;
    std::vector<std::vector<int>> m_vc_winners; // a list for each outport
    std::vector<InputUnit *> m_input_unit;
    std::vector<OutputUnit *> m_output_unit;

    //added for tdm
    //std::vector<vector<int>> m_wave_table;
    std::vector<int>num_ports_req;
    std::vector<int>num_local_req;
    std::vector<bool> outport_ava;

    //added for deflection
    std::vector<pair<flit*, int>> m_permu_buf;
    std::vector<bool> inport_observed;
    bool compareFlitID(pair<flit*, int> a, pair<flit*, int> b);
    int getANonLocalOutport(PortDirection in_dirn);

    void areLinksAvaliable();


};

#endif // __MEM_RUBY_NETWORK_GARNET2_0_SWITCHALLOCATOR_HH__
