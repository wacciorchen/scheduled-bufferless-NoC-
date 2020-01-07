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


#include "mem/ruby/network/garnet2.0/Router.hh"

#include "base/stl_helpers.hh"
#include "debug/RubyNetwork.hh"
#include "mem/ruby/network/garnet2.0/CreditLink.hh"
#include "mem/ruby/network/garnet2.0/CrossbarSwitch.hh"
#include "mem/ruby/network/garnet2.0/GarnetNetwork.hh"
#include "mem/ruby/network/garnet2.0/InputUnit.hh"
#include "mem/ruby/network/garnet2.0/NetworkLink.hh"
#include "mem/ruby/network/garnet2.0/OutputUnit.hh"
#include "mem/ruby/network/garnet2.0/RoutingUnit.hh"
#include "mem/ruby/network/garnet2.0/SwitchAllocator.hh"

using namespace std;
using m5::stl_helpers::deletePointers;

Router::Router(const Params *p)
    : BasicRouter(p), Consumer(this)
{
    m_latency = p->latency;
    m_virtual_networks = p->virt_nets;
    m_vc_per_vnet = p->vcs_per_vnet;
    m_num_vcs = m_virtual_networks * m_vc_per_vnet;
    mrkd_flt_ = 0;//p->marked_flit;//TODO
    m_routing_unit = new RoutingUnit(this);
    m_sw_alloc = new SwitchAllocator(this);
    m_switch = new CrossbarSwitch(this);

    m_input_unit.clear();
    m_output_unit.clear();
    outport_allocation_tracker.resize(10);
    //TODO
    m_router_inport_dirn2id["West"] = 0;
    m_router_inport_dirn2id["South"] = 1;
    m_router_inport_dirn2id["East"] = 2;
    m_router_inport_dirn2id["North"] = 3;
    m_router_inport_dirn2id["Local"] = 4;
    //for wave allocation
    m_router_outport_dirn2id["South"] = 5;
    m_router_outport_dirn2id["West"] = 3;
    m_router_outport_dirn2id["East"] = 4;
    m_router_outport_dirn2id["North"] = 2;

}

Router::~Router()
{
    deletePointers(m_input_unit);
    deletePointers(m_output_unit);
    delete m_routing_unit;
    delete m_sw_alloc;
    delete m_switch;
}

void
Router::init()
{
    BasicRouter::init();

    m_sw_alloc->init();
    m_switch->init();
}

void
Router::wakeup()
{
    DPRINTF(RubyNetwork, "Router %d woke up\n", m_id);
    //int cur_cycle = (int)curCycle();

    //std::cout<<"router:: current Cycle" << curCycle()<<" id is "<<m_id<<" cur_cycle is "<<cur_cycle<<endl;
    //outport_allocation_tracker.resize(m_input_unit.size());

    // check for incoming flits
    for (int inport = 0; inport < m_input_unit.size(); inport++) {
        m_input_unit[inport]->wakeup();
    }

    // check for incoming credits
    // Note: the credit update is happening before SA
    // buffer turnaround time =
    //     credit traversal (1-cycle) + SA (1-cycle) + Link Traversal (1-cycle)
    // if we want the credit update to take place after SA, this loop should
    // be moved after the SA request
    for (int outport = 0; outport < m_output_unit.size(); outport++) {
        m_output_unit[outport]->wakeup();
    }

    // Switch Allocation
    m_sw_alloc->wakeup();

    // Switch Traversal
    m_switch->wakeup();
}

void
Router::addInPort(PortDirection inport_dirn,
                  NetworkLink *in_link, CreditLink *credit_link)
{
    int port_num = m_input_unit.size();
    InputUnit *input_unit = new InputUnit(port_num, inport_dirn, this);

    input_unit->set_in_link(in_link);
    input_unit->set_credit_link(credit_link);
    in_link->setLinkConsumer(this);
    credit_link->setSourceQueue(input_unit->getCreditQueue());

    m_input_unit.push_back(input_unit);

    m_routing_unit->addInDirection(inport_dirn, port_num);
}

void
Router::addOutPort(PortDirection outport_dirn,
                   NetworkLink *out_link,
                   const NetDest& routing_table_entry, int link_weight,
                   CreditLink *credit_link)
{
    int port_num = m_output_unit.size();
    OutputUnit *output_unit = new OutputUnit(port_num, outport_dirn, this);

    output_unit->set_out_link(out_link);
    output_unit->set_credit_link(credit_link);
    credit_link->setLinkConsumer(this);
    out_link->setSourceQueue(output_unit->getOutQueue());

    m_output_unit.push_back(output_unit);
    m_routing_unit->addRoute(routing_table_entry);
    m_routing_unit->addWeight(link_weight);
    m_routing_unit->addOutDirection(outport_dirn, port_num);
} // old version

void
Router::addOutPort(PortDirection outport_dirn,
                   NetworkLink *out_link,
                   const NetDest& routing_table_entry, int link_weight,
                   CreditLink *credit_link, const std::vector<int>& m_wave)
{
    int port_num = m_output_unit.size();
    OutputUnit *output_unit = new OutputUnit(port_num, outport_dirn, this);
    //std::cout<<"\noutport_dirn is "<< outport_dirn << " port_num is "<< port_num <<endl;
    output_unit->set_out_link(out_link);
    output_unit->set_credit_link(credit_link);
    credit_link->setLinkConsumer(this);
    out_link->setSourceQueue(output_unit->getOutQueue());
    m_output_unit.push_back(output_unit);
    //std::cout<<"\nrouter add outport direction "<<outport_dirn<<" port num is "<<port_num<<endl;
    //std::cout<<"corresponding num is "<<m_router_outport_dirn2id[outport_dirn]<<endl;
    m_routing_unit->addRoute(routing_table_entry);
    m_routing_unit->addWeight(link_weight);
    m_routing_unit->addOutDirection(outport_dirn, port_num);
    if(outport_dirn != "Local"){ // only alloc wave on non-local link
        m_routing_unit->addWave(m_wave, m_routing_unit->outport_dirn2id(outport_dirn));
    }
}

PortDirection
Router::getOutportDirection(int outport)
{
    return m_output_unit[outport]->get_direction();
}

PortDirection
Router::getInportDirection(int inport)
{
    return m_input_unit[inport]->get_direction();
}

int
Router::route_compute(RouteInfo route, int inport, PortDirection inport_dirn)
{
    return m_routing_unit->outportCompute(route, inport, inport_dirn);
}

//modify
int
Router::route_compute(RouteInfo route, int inport, PortDirection inport_dirn, int invc)
{
    return m_routing_unit->outportCompute(route, inport, inport_dirn, invc);
}

// int
// Router::route_compute(RouteInfo route, int inport, PortDirection inport_dirn, int invc, Cycles cur_cycle)
// {
//     return m_routing_unit->outportCompute(route, inport, inport_dirn, invc, cur_cycle);
// }

void
Router::grant_switch(int inport, flit *t_flit)
{
    m_switch->update_sw_winner(inport, t_flit);
}
//Modify
bool
Router::has_free_vc(int outport, int vnet)
{
    return m_output_unit[outport]->has_free_vc(vnet);
}

void
Router::schedule_wakeup(Cycles time)
{
    // wake up after time cycles
    scheduleEvent(time);
}

std::string
Router::getPortDirectionName(PortDirection direction)
{
    // PortDirection is actually a string
    // If not, then this function should add a switch
    // statement to convert direction to a string
    // that can be printed out
    return direction;
}

void
Router::regStats()
{
    BasicRouter::regStats();

    m_buffer_reads
        .name(name() + ".buffer_reads")
        .flags(Stats::nozero)
    ;

    m_buffer_writes
        .name(name() + ".buffer_writes")
        .flags(Stats::nozero)
    ;

    m_crossbar_activity
        .name(name() + ".crossbar_activity")
        .flags(Stats::nozero)
    ;

    m_sw_input_arbiter_activity
        .name(name() + ".sw_input_arbiter_activity")
        .flags(Stats::nozero)
    ;

    m_sw_output_arbiter_activity
        .name(name() + ".sw_output_arbiter_activity")
        .flags(Stats::nozero)
    ;
}

void
Router::collateStats()
{
    for (int j = 0; j < m_virtual_networks; j++) {
        for (int i = 0; i < m_input_unit.size(); i++) {
            m_buffer_reads += m_input_unit[i]->get_buf_read_activity(j);
            m_buffer_writes += m_input_unit[i]->get_buf_write_activity(j);
        }
    }

    m_sw_input_arbiter_activity = m_sw_alloc->get_input_arbiter_activity();
    m_sw_output_arbiter_activity = m_sw_alloc->get_output_arbiter_activity();
    m_crossbar_activity = m_switch->get_crossbar_activity();
}

void
Router::resetStats()
{
    for (int j = 0; j < m_virtual_networks; j++) {
        for (int i = 0; i < m_input_unit.size(); i++) {
            m_input_unit[i]->resetStats();
        }
    }

    m_switch->resetStats();
    m_sw_alloc->resetStats();
}

void
Router::printFaultVector(ostream& out)
{
    int temperature_celcius = BASELINE_TEMPERATURE_CELCIUS;
    int num_fault_types = m_network_ptr->fault_model->number_of_fault_types;
    float fault_vector[num_fault_types];
    get_fault_vector(temperature_celcius, fault_vector);
    out << "Router-" << m_id << " fault vector: " << endl;
    for (int fault_type_index = 0; fault_type_index < num_fault_types;
         fault_type_index++) {
        out << " - probability of (";
        out <<
        m_network_ptr->fault_model->fault_type_to_string(fault_type_index);
        out << ") = ";
        out << fault_vector[fault_type_index] << endl;
    }
}

void
Router::printAggregateFaultProbability(std::ostream& out)
{
    int temperature_celcius = BASELINE_TEMPERATURE_CELCIUS;
    float aggregate_fault_prob;
    get_aggregate_fault_probability(temperature_celcius,
                                    &aggregate_fault_prob);
    out << "Router-" << m_id << " fault probability: ";
    out << aggregate_fault_prob << endl;
}

uint32_t
Router::functionalWrite(Packet *pkt)
{
    uint32_t num_functional_writes = 0;
    num_functional_writes += m_switch->functionalWrite(pkt);

    for (uint32_t i = 0; i < m_input_unit.size(); i++) {
        num_functional_writes += m_input_unit[i]->functionalWrite(pkt);
    }

    for (uint32_t i = 0; i < m_output_unit.size(); i++) {
        num_functional_writes += m_output_unit[i]->functionalWrite(pkt);
    }

    return num_functional_writes;
}

//modify
void Router::display_struct(){

    std::cout << "\n-----id=" << m_id << "-------" << curCycle() << "---------";
    for(int i=0; i<5; ++i){
        std::cout << "\nO = " << outport_allocation_tracker[i].outport;
        std::cout << "\tT= " << outport_allocation_tracker[i].time;
    }
    std::cout << "\n------------" << curCycle() << "---------";

}

bool Router::lookup_outport_allocation_tracker(int out){

    // for(int i=0; i<5; ++i){
    //     if(outport_allocation_tracker[i].outport == out){
    //         if(outport_allocation_tracker[i].time == curCycle()){
    //             if(i == 4)
    //                 return true; //only Local avaliable, assume infinite receiver buffer
    //             else
    //                 return false;
    //         }
    //     }
    // }
    // return true;
    assert(outport_allocation_tracker.size() >= m_input_unit.size());
    for(int i = 0; i < m_input_unit.size(); i++){
        if(outport_allocation_tracker[i].outport == out){
            if(outport_allocation_tracker[i].time == curCycle()){
                if(m_routing_unit->inport_id2dirn(i) != "Local")
                    return false;
            }
        }
    }

    return true;
}


//unnecessary to use out_dirn as parameter, outport id also works
bool Router::nextWaveChecker(Cycles nextWave, PortDirection out_dirn){ // TODO require a direction change here
    int port = m_routing_unit->outport_dirn2id(out_dirn);
    m_wave_table = m_routing_unit->get_wvTable_ref();
    assert(out_dirn != "Local" && m_wave_table[port].size() > 0); // make sure the port not from Local, and has wave on it

    Cycles outportWave = Cycles(0);

    for(int i = 0; i < m_wave_table[port].size(); i++){
        outportWave = Cycles(m_wave_table[port][i]);
        if(outportWave == nextWave)
            return true;
    }

    return false;

}

bool Router::isLinkAvaliable(int port_num){
    if(m_output_unit[port_num]->get_direction() == "Local"){
        return true;
    }else{
        Cycles cur_wave = curWave();
        Cycles wave_num = getWaveNum();
        Cycles next_wave = (cur_wave + Cycles(1)) % wave_num;
        
        m_wave_table = m_routing_unit->get_wvTable_ref();
        assert(m_wave_table[port_num].size() > 0);
        Cycles outportWave = Cycles(0);

        for(int i = 0; i < m_wave_table[port_num].size(); i++){
            outportWave = Cycles(m_wave_table[port_num][i]);
            if(outportWave == next_wave)
                return true;
        }
    }

    return false;
}

int Router::getWaveDirection(const std::vector<int> &pref, PortDirection in_dirn, int inport){
    Cycles cur_cycle = curCycle();
    if(cur_cycle == Cycles(0)){
        return 0;
    } // TODO take attention here 
    Cycles cur_wave = curWave();
    Cycles wave_num = getWaveNum();
    Cycles next_wave = (cur_wave + Cycles(1)) % wave_num;

    std::vector<int> output_link_candidates;
    int num_candidates = 0;
    PortDirection curr_dirn;
    bool outport_avaliable = false;

    //inportIndex = m_routing_unit->inport_dirn2id(in_dirn);
    assert(in_dirn == getInportDirection(inport));
    m_input_unit[inport]->reset_flag();

    for(int i = 0; i < pref.size(); i++){
        /*
        if(m_routing_unit->outport_id2dirn(pref[i]) == "Local"){ // dest is Local, assume infinite receiver buffer
            m_input_unit[inportIndex]->set_flag(true);
            inport = m_router_inport_dirn2id[in_dirn];
            outport_allocation_tracker[inport].outport = pref[i];
            outport_allocation_tracker[inport].time = curCycle();
            return pref[i];
        }
        */
        bool pref_available = lookup_outport_allocation_tracker(pref[i]);
        if(pref_available){
            //check this outport has corresponding next wave
            //local outport doesn't require to check next wave
            if(m_routing_unit->outport_id2dirn(pref[i]) == "Local")
                outport_avaliable = true;
            else
                outport_avaliable = nextWaveChecker(next_wave, m_routing_unit->outport_id2dirn(pref[i])); //TODO require convert here
            
            if(outport_avaliable){
                num_candidates++;
                output_link_candidates.push_back(pref[i]);
            }
        }
    }

    if(output_link_candidates.size() > 0){
        int candidate = 0;
        //TODO make sure delete vnetorder no effect
        candidate = rand() % num_candidates;
        
        m_input_unit[inport]->set_flag(true);

        outport_allocation_tracker[inport].outport = output_link_candidates.at(candidate);
        outport_allocation_tracker[inport].time = curCycle();
        return output_link_candidates.at(candidate);
    }

    int backup = -1;
    //Check for possible misroutes

    for (int outport = 0; outport < m_output_unit.size(); outport++) {
        
        curr_dirn = m_routing_unit->outport_id2dirn(outport);
        
        if(lookup_outport_allocation_tracker(outport)){
            if(curr_dirn != "Local"){
                //each non-local outport direction only has one correspoding outport id
                outport_avaliable = nextWaveChecker(next_wave, curr_dirn);
                if(outport_avaliable){
                    m_input_unit[inport]->set_flag(true);
                    backup = outport;
                    break;
                }
            }
        }
    }

    //flit in local has to stall in this cycle
    if(backup == -1 && in_dirn == "Local"){
        //havent found a proper outport
        m_input_unit[inport]->set_flag(false);
        backup = pref[0];
    }
    assert(backup != -1);

    outport_allocation_tracker[inport].outport = backup;
    outport_allocation_tracker[inport].time = curCycle();

    return backup;
}

int Router::getAllocatedDirection(int pref, PortDirection in_dirn, int inport){

    int backup = -1;
    PortDirection curr_dirn;

    bool pref_available = lookup_outport_allocation_tracker(pref);
    
    //If preferred outport is available, update tracker and leave
    if(pref_available){
        outport_allocation_tracker[inport].outport = pref;
        outport_allocation_tracker[inport].time = curCycle();
        return pref;
    }

    //Check for possible misroutes
    // for(auto it=m_output_unit.begin(); it!=m_output_unit.end(); ++it){

    //     curr_dirn = (*it)->get_direction();
    //     curr_outport = m_routing_unit->outport_dirn2id(curr_dirn);

    //     if(lookup_outport_allocation_tracker(curr_outport)){
    //         if(curr_dirn != "Local"){
    //             backup = curr_outport;
    //             if(in_dirn != curr_dirn)    break;
    //         }
    //     }

    // }

    for (int outport = 0; outport < m_output_unit.size(); outport++) {
        
        curr_dirn = m_routing_unit->outport_id2dirn(outport);
        
        if(lookup_outport_allocation_tracker(outport)){
            if(curr_dirn != "Local"){
                backup = outport;
                if(in_dirn != curr_dirn)    break;
            }
        }
    }

    //No backup ports available, allocate the flit that in being Injected
    //as it would be given least priority
    if((backup==-1) && in_dirn=="Local")
        backup = pref;

    //make sure all incoming flit have been allocated an outport
    assert(backup != -1);

    outport_allocation_tracker[inport].outport = backup;
    outport_allocation_tracker[inport].time = curCycle();

    return backup;

///////////////////////////////////////////////////////////////////////////////

    /* has_free_vc() approach*/
    /*
    bool pref_available = m_output_unit[pref]->has_free_vc(0);

    if(pref_available)
        return pref;

    //Code to use has_free_vc() to do misrouting

    for(auto it=m_output_unit.begin(); it != m_output_unit.end(); ++it){

        curr_dirn = (*it)->get_direction();
        curr_outport = m_routing_unit->outport_dirn2id(curr_dirn);

        if((*it)->has_free_vc(0)){
            if(curr_dirn != "Local"){
                backup = curr_outport;
                //Try to not prefer U-turns
                if(in_dirn != curr_dirn)
                    break;
            }
        }

    }

    if(backup == -1){
        backup = pref;
    }
    //Every flit must leave the router
    assert(backup != -1 || m_routing_unit->outport_id2dirn(pref)=="Local");

    return backup;
    */

//////////////////////////////////////////////////////////////////////////

    /*INITIAL attempt - CHECK ALL ALLOCATED OUTPORTS*/

    /* //Code to monitor InputUnits and allocated outports


    //This loop tracks all allocated outports for input VCs
    for(auto it1=m_input_unit.begin(); it1 != m_input_unit.end(); ++it1){

        curr_outport = (*it1)->get_outport(0);

        if(curr_outport != -1){

            curr_dirn = m_routing_unit->outport_id2dirn(curr_outport);

            for(i=0; i<4; ++i){
                if(dirns[i] == curr_dirn){
                    alloc_dirns[i] = 0;
                    break;
                }
            }

            if( curr_outport == pref ){
                pref_available = false;
            }

        }

    }

    //Return if preferred outport has not been allocated
    if(pref_available)
        return pref;

    //This loop checks OutputUnit and marks available outports
    for(auto it2=m_output_unit.begin(); it2 != m_output_unit.end(); ++it2){

        curr_dirn = (*it2)->get_direction();

        for(i=0; i<4; ++i){
            if(alloc_dirns[i] == -1)
                if(curr_dirn == dirns[i]){
                    alloc_dirns[i] = 1;
                    break;
                }
        }
    }

    //Assign outport if preferred outport is not available
    for(i=0; i<4; ++i){
        if(alloc_dirns[i] == 1){
            backup = m_routing_unit->outport_dirn2id(dirns[i]);
        }
    }


    assert(backup != -1);
    return backup;
    */ //Code to monitor InputUnit and allocated outports ends

}

Router *
GarnetRouterParams::create()
{
    return new Router(this);
}


int Router::router_inport_dirn2id(PortDirection dirn){
    return (m_routing_unit->inport_dirn2id(dirn));
}

PortDirection Router::router_inport_id2dirn(int port_num){
    return (m_routing_unit->inport_id2dirn(port_num));
}

int Router::router_outport_dirn2id(PortDirection dirn){
    return (m_routing_unit->outport_dirn2id(dirn));
}

PortDirection Router::router_outport_id2dirn(int port_num){
    return (m_routing_unit->outport_id2dirn(port_num));
}
