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


#include "mem/ruby/network/garnet2.0/flit.hh"

// Constructor for the flit
// flit::flit(int id, int  vc, int vnet, RouteInfo route, int size,
//     MsgPtr msg_ptr, Cycles curTime)
// {
//     m_size = size;
//     m_msg_ptr = msg_ptr;
//     m_enqueue_time = curTime;
//     m_dequeue_time = curTime;
//     m_time = curTime;
//     m_id = id;
//     m_vnet = vnet;
//     m_vc = vc;
//     m_route = route;
//     m_stage.first = I_;
//     m_stage.second = m_time;

//     if (size == 1) {
//         m_type = HEAD_TAIL_;
//         return;
//     }
//     if (id == 0)
//         m_type = HEAD_;
//     else if (id == (size - 1))
//         m_type = TAIL_;
//     else
//         m_type = BODY_;
// } // Original version
//New Added
flit::flit(int id, int  vc, int vnet, RouteInfo route, int size,
    MsgPtr msg_ptr, Cycles curTime, bool marked)
{
    m_size = size;
    m_msg_ptr = msg_ptr;
    m_enqueue_time = curTime;
    m_dequeue_time = curTime;
    m_time = curTime;
    m_id = id;
    m_vnet = vnet;
    m_vc = vc;
    m_route = route;
    m_stage.first = I_;
    m_stage.second = m_time;
    m_marked = marked;

    //added for deflection
    //NoC saturate at 100 cycles
    //pick 10000 as threashold for flit id update
    //no duplicate id will appear 
    int offset = (int) curTime % Cycles(FLIT_ID_);
    int index = route.src_ni * FLIT_ID_;
    m_clk_id = index + offset;
    
    //set up pusedo gold state threashold for each flit
    //the reason to add id is because it count the # of cycle in NoC
    //not the hop count, in our case, every trans stage is 2 cycle, so id should be divided by 2
    //assume 4x4 mesh
    int src_row = m_route.src_router / 4;
    int dst_row = m_route.dest_router / 4;
    int src_col = m_route.src_router % 4;
    int dst_col = m_route.dest_router % 4;
    
    m_gold_th = abs(dst_row - src_row) + abs(dst_col - src_col);
    is_gold = false;
    //m_type = HEAD_TAIL_;
    
    if (size == 1) {
        m_type = HEAD_TAIL_;
        return;
    }
    if (id == 0)
        m_type = HEAD_;
    else if (id == (size - 1))
        m_type = TAIL_;
    else
        m_type = BODY_;
    

}
//Added End
// Flit can be printed out for debugging purposes
void
flit::print(std::ostream& out) const
{
    out << "[flit:: ";
    out << "Id=" << m_id << " ";
    out << "Id=" << m_clk_id << " ";
    out << "size=" << m_size << " ";
    out << "Type=" << m_type << " ";
    out << "Vnet=" << m_vnet << " ";
    out << "VC=" << m_vc << " ";
    out << "Src NI=" << m_route.src_ni << " ";
    out << "Src Router=" << m_route.src_router << " ";
    out << "Dest NI=" << m_route.dest_ni << " ";
    out << "Dest Router=" << m_route.dest_router << " ";
    out << "Enqueue Time=" << m_enqueue_time << " ";
    out << "]";
}

bool
flit::functionalWrite(Packet *pkt)
{
    Message *msg = m_msg_ptr.get();
    return msg->functionalWrite(pkt);
}
