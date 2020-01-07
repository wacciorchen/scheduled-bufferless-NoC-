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


#include "mem/ruby/network/garnet2.0/SwitchAllocator.hh"

#include "debug/RubyNetwork.hh"
//#include "mem/ruby/network/garnet2.0/GarnetNetwork.hh"
// #include "mem/ruby/network/garnet2.0/InputUnit.hh"
// #include "mem/ruby/network/garnet2.0/OutputUnit.hh"
// #include "mem/ruby/network/garnet2.0/Router.hh"

SwitchAllocator::SwitchAllocator(Router *router)
    : Consumer(router)
{
    m_router = router;
    m_num_vcs = m_router->get_num_vcs();
    m_vc_per_vnet = m_router->get_vc_per_vnet();

    m_input_arbiter_activity = 0;
    m_output_arbiter_activity = 0;
}

void
SwitchAllocator::init()
{
    m_input_unit = m_router->get_inputUnit_ref();
    m_output_unit = m_router->get_outputUnit_ref();

    m_num_inports = m_router->get_num_inports();
    m_num_outports = m_router->get_num_outports();
    m_round_robin_inport.resize(m_num_outports);
    m_round_robin_invc.resize(m_num_inports);
    m_port_requests.resize(m_num_outports);
    m_vc_winners.resize(m_num_outports);
    num_ports_req.resize(m_num_outports);
    num_local_req.resize(m_num_outports);

    inport_observed.resize(m_num_inports);
    outport_ava.resize(m_num_outports);

    for (int i = 0; i < m_num_inports; i++) {
        m_round_robin_invc[i] = 0;
    }

    for (int i = 0; i < m_num_outports; i++) {
        m_port_requests[i].resize(m_num_inports);
        m_vc_winners[i].resize(m_num_inports);

        m_round_robin_inport[i] = 0;

        for (int j = 0; j < m_num_inports; j++) {
            m_port_requests[i][j] = false; // [outport][inport]
        }
    }
}

/*
 * The wakeup function of the SwitchAllocator performs a 2-stage
 * seperable switch allocation. At the end of the 2nd stage, a free
 * output VC is assigned to the winning flits of each output port.
 * There is no separate VCAllocator stage like the one in garnet1.0.
 * At the end of this function, the router is rescheduled to wakeup
 * next cycle for peforming SA for any flits ready next cycle.
 */

void
SwitchAllocator::wakeup()
{
    //TODO
    RoutingAlgorithm routing_algo =
        (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();    

    arbitrate_inports(); // First stage of allocation
    arbitrate_outports(); // Second stage of allocation

    clear_request_vector();
    check_for_wakeup();
    
    if(routing_algo == DEFLECTION_ || routing_algo == TDM_){    
        verify_VCs_empty();
    }

}

/*
 * SA-I (or SA-i) loops through all input VCs at every input port,
 * and selects one in a round robin manner.
 *    - For HEAD/HEAD_TAIL flits only selects an input VC whose output port
 *     has at least one free output VC.
 *    - For BODY/TAIL flits, only selects an input VC that has credits
 *      in its output VC.
 * Places a request for the output port from this input VC.
 */

void
SwitchAllocator::arbitrate_inports()
{
    m_permu_buf.clear();//clear permutation buffer

    RoutingAlgorithm routing_algo =
        (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();
    //added for deflection, recalculate outport every cycle
    if(routing_algo == DEFLECTION_ || routing_algo == TDM_){
        for (int i = 0; i < m_num_outports; i++) {
            m_port_requests[i].resize(m_num_inports);
            m_vc_winners[i].resize(m_num_inports);

            m_round_robin_inport[i] = 0;

            for (int j = 0; j < m_num_inports; j++) {
                m_port_requests[i][j] = false; // [outport][inport]
            }
        }
    }
    // Select a VC from each input in a round robin manner
    // Independent arbiter at each input port
    PortDirection in_dirn;

    //cout<<"Before permutation "<<endl;
    for (int inport = 0; inport < m_num_inports; inport++) {
        int invc = m_round_robin_invc[inport];
        //reset flag for tdm_
        m_input_unit[inport]->reset_flag();
        for (int invc_iter = 0; invc_iter < m_num_vcs; invc_iter++) {

            if (m_input_unit[inport]->need_stage(invc, SA_,
                m_router->curCycle())) {
                
                in_dirn = m_router->router_inport_id2dirn(inport);
                
                flit *t_flit = m_input_unit[inport]->peekTopFlit(invc);
                if(routing_algo == TDM_ || routing_algo == DEFLECTION_){
                    //cout<<"flit id is "<<t_flit->getClkId()<<" inport id is "<<inport
                    //                   <<" invc is "<<t_flit->get_vc()
                    //                  <<" prefer outport is "<<m_input_unit[inport]->get_outport(invc)<<endl;
                    m_permu_buf.push_back({t_flit, inport});
                    //break the invc iterator to distribute data allocatino
                    break;
                }else {
                    // This flit is in SA stage

                    int  outport = m_input_unit[inport]->get_outport(invc);
                    int  outvc   = m_input_unit[inport]->get_outvc(invc);
                    //std::cout<<"\n\nthis outport "<<outport<<" this outvc "<<outvc<<" from inport" << inport << "invc "<<invc<<endl;
                    // check if the flit in this InputVC is allowed to be sent
                    // send_allowed conditions described in that function.
                    // std::cout<<"my router id " << m_router->get_id()<<endl;
                    bool make_request =
                        send_allowed(inport, invc, outport, outvc);

                    if (routing_algo == DEFLECTION_) //TODO
                        assert(make_request);
                    if (routing_algo == TDM_ && m_input_unit[inport]->get_direction() != "Local")
                        assert(make_request);

                    if (make_request) {
                        m_input_arbiter_activity++;
                        m_port_requests[outport][inport] = true;
                        m_vc_winners[outport][inport]= invc;

                        // Update Round Robin pointer
                        m_round_robin_invc[inport] = invc + 1;
                        if (m_round_robin_invc[inport] >= m_num_vcs)
                            m_round_robin_invc[inport] = 0;

                        break; // got one vc winner for this port
                    }
                }
            }

            invc++;
            if (invc >= m_num_vcs)
                invc = 0;
        }
    }
    
    //after permutation
    //cout<<"After permutation"<<endl;
    if(routing_algo == TDM_ || routing_algo == DEFLECTION_){
        if(routing_algo == TDM_)
            areLinksAvaliable();
        else
            fill(outport_ava.begin(), outport_ava.end(), true);
        permutation_CHIPPER();
        //permutation_BLESS();
        //now flit is in SA stage
        for(int i = 0; i < m_permu_buf.size(); i++){
            flit *t_flit = m_permu_buf[i].first;
            int inport = m_permu_buf[i].second;
            int invc = t_flit->get_vc();
            int  outport = m_input_unit[inport]->get_outport(invc);
            int  outvc   = m_input_unit[inport]->get_outvc(invc);

            bool make_request =
                send_allowed(inport, invc, outport, outvc);

            if (routing_algo == DEFLECTION_) //TODO
                assert(make_request);
            if (routing_algo == TDM_ && m_input_unit[inport]->get_direction() != "Local")
                assert(make_request);

            //cout<<"flit id is "<<t_flit->getClkId()<<" inport id is "<<inport
            //                   <<" invc is "<<t_flit->get_vc()
            //                   <<" prefer outport is "<<m_input_unit[inport]->get_outport(invc)<<endl;
            if (make_request) {
                m_input_arbiter_activity++;
                m_port_requests[outport][inport] = true;
                m_vc_winners[outport][inport]= invc;

                // Update Round Robin pointer
                m_round_robin_invc[inport] = invc + 1;
                if (m_round_robin_invc[inport] >= m_num_vcs)
                    m_round_robin_invc[inport] = 0;
            }
        }
    }   
}

bool
SwitchAllocator::compareFlitID(pair<flit*, int> a, pair<flit*, int> b){
    return (a.first->get_id() <= b.first->get_id());
}

int
SwitchAllocator::getANonLocalOutport(PortDirection in_dirn){
    int num_candidate = 0;
    std::vector<int> candidateOutPorts;
    candidateOutPorts.clear();
    bool isUTurn = false;
    int uTurnId = -1;
    for(int i = 0; i < outport_ava.size(); i++){
    	PortDirection out_dirn = m_output_unit[i]->get_direction();
        if(out_dirn != "Local" && outport_ava[i] == true && out_dirn != in_dirn){
            candidateOutPorts.push_back(i);
            num_candidate++;        
        }else if(out_dirn != "Local" && outport_ava[i] == true && out_dirn == in_dirn){
        	isUTurn = true;
        	uTurnId = i;
        }
    }

    int candidate = 0;
    if(num_candidate > 0){
    	candidate = rand() % num_candidate;
    	return candidateOutPorts.at(candidate);
    }

//  only uturn link is avaliable
    if(isUTurn){
    	assert(uTurnId != -1);
    	return uTurnId;
    }

    return -1;
}

void
SwitchAllocator::areLinksAvaliable(){
    assert(outport_ava.size() == m_num_outports);
    fill(outport_ava.begin(), outport_ava.end(), true);
    for(int i = 0; i < outport_ava.size(); i++){
        if(!m_router->isLinkAvaliable(i))
            outport_ava[i] = false;
    }
}

void //TODO
SwitchAllocator::permutation_CHIPPER()
{
    std::vector<pair<flit*, int>> gold_flits;
    std::vector<pair<flit*, int>> non_gold_flits;
    std::vector<pair<flit*, int>> local_flits;

    //print flit type
    for(int i = 0; i < m_permu_buf.size(); i++){
        int inport = m_permu_buf[i].second;
        flit *t_flit = m_permu_buf[i].first;
        //cout<<"SA, is gold state "<<t_flit->is_gold_state()<<"inport id "<<inport<<m_input_unit[inport]->get_direction()<<endl;
        DPRINTF(RubyNetwork, "PERMUTATION SwitchAllocator at Router %d "
                                     " at inport %d to flit %s at "
                                     "time: %lld\n",
                        m_router->get_id(),
                        m_router->getPortDirectionName(
                            m_input_unit[inport]->get_direction()),
                            *t_flit,
                        m_router->curCycle());
    }

    //split flit type  
    for(int i = 0; i < m_permu_buf.size(); i++){
        int inport = m_permu_buf[i].second;
        flit *t_flit = m_permu_buf[i].first;
        if(t_flit->is_gold_state())
            gold_flits.push_back(m_permu_buf[i]);
        else if(m_input_unit[inport]->get_direction() == "Local")
            local_flits.push_back(m_permu_buf[i]);
        else
            non_gold_flits.push_back(m_permu_buf[i]);
    }
    
    //sort gold state in decend order
    sort(gold_flits.begin(), gold_flits.end(), std::bind(&SwitchAllocator::compareFlitID, this, std::placeholders::_1, std::placeholders::_2));
    bool isUTurn = false;
    //alloc gold flit 
    for(int i = 0; i < gold_flits.size(); i++){
        int inport = gold_flits[i].second;
        PortDirection in_dirn = m_input_unit[inport]->get_direction();
        //one possible case for local flit to be gold
        //Local to Local
        //in this case, release them only if the local outport is avaliable, otherwise stall at local
        int invc = gold_flits[i].first->get_vc();
        int prefer_outport = m_input_unit[inport]->get_outport(invc);
        int rand_outport = -1;
        if(m_output_unit[prefer_outport]->get_direction() == in_dirn){
        	isUTurn = true;
        }else{
        	isUTurn = false;
        }

        //todo LOCAL TO LOCAL as a special case
        if(in_dirn == "Local"){
            assert(m_output_unit[prefer_outport]->get_direction() == "Local");
            m_input_unit[inport]->grant_outport(invc, prefer_outport);
            m_input_unit[inport]->set_flag(true);
        }else{
            if(outport_ava[prefer_outport] && !isUTurn){ //u turn has least priority
                outport_ava[prefer_outport] = false;
                m_input_unit[inport]->set_flag(true);
                m_input_unit[inport]->grant_outport(invc, prefer_outport);
            }else{
                rand_outport = getANonLocalOutport(in_dirn);
                assert(rand_outport != -1 && m_output_unit[rand_outport]->get_direction() != "Local");
                outport_ava[rand_outport] = false;
                m_input_unit[inport]->set_flag(true);
                m_input_unit[inport]->grant_outport(invc, rand_outport);                
            	//cout<<"SA: Router "<<m_router->get_id()<<" inport "<<inport<<", flit "
            	//	<<gold_flits[i].first->get_type()<<", prefer "<<prefer_outport<<", deflect to "<<rand_outport<<endl;
            }
        }
    }

    //alloc non-gold-flit
    for(int i = 0; i < non_gold_flits.size(); i++){
        int inport = non_gold_flits[i].second;
        assert(m_input_unit[inport]->get_direction() != "Local");
        PortDirection in_dirn = m_input_unit[inport]->get_direction();
        int invc = non_gold_flits[i].first->get_vc();
        int prefer_outport = m_input_unit[inport]->get_outport(invc);
        int rand_outport = -1;

        if(m_output_unit[prefer_outport]->get_direction() == in_dirn){
        	isUTurn = true;
        }else{
        	isUTurn = false;
        }

        if(outport_ava[prefer_outport] && !isUTurn){
            outport_ava[prefer_outport] = false;
            m_input_unit[inport]->set_flag(true);
            m_input_unit[inport]->grant_outport(invc, prefer_outport);
        }else{
            rand_outport = getANonLocalOutport(in_dirn);
            assert(rand_outport != -1 && m_output_unit[rand_outport]->get_direction() != "Local");
            outport_ava[rand_outport] = false;
            m_input_unit[inport]->set_flag(true);
            m_input_unit[inport]->grant_outport(invc, rand_outport);
            //cout<<"SA: Router "<<m_router->get_id()<<" inport "<<inport<<", flit "
            //		<<non_gold_flits[i].first->get_type()<<", prefer "<<prefer_outport<<", deflect to "<<rand_outport<<endl;
            
        }
    }

    //alloc local flit
    //allow non-avaliable outport
    for(int i = 0; i < local_flits.size(); i++){
        int inport = local_flits[i].second;
        assert(m_input_unit[inport]->get_direction() == "Local");
        int invc = local_flits[i].first->get_vc();
        int prefer_outport = m_input_unit[inport]->get_outport(invc);
        int rand_outport = -1;
        if(outport_ava[prefer_outport]){
            outport_ava[prefer_outport] = false;
            m_input_unit[inport]->set_flag(true);
            m_input_unit[inport]->grant_outport(invc, prefer_outport);
        }else{
            rand_outport = getANonLocalOutport(m_input_unit[inport]->get_direction());
            if(rand_outport == -1){
                m_input_unit[inport]->set_flag(false);
                m_input_unit[inport]->grant_outport(invc, prefer_outport);
            }else{
                outport_ava[rand_outport] = false;
                m_input_unit[inport]->set_flag(true);
                m_input_unit[inport]->grant_outport(invc, rand_outport);
                //cout<<"SA: Router "<<m_router->get_id()<<" inport "<<inport<<", flit "
            	//	<<local_flits[i].first->get_type()<<", prefer "<<prefer_outport<<", deflect to "<<rand_outport<<endl;
 
            }
        }
    }
}
//TODO:modify uturn policy for bless future
/*
void
SwitchAllocator::permutation_BLESS()
{
    std::vector<pair<flit*, int>> order_flits;
    std::vector<pair<flit*, int>> local_flits;

    //print flit type
    for(int i = 0; i < m_permu_buf.size(); i++){
        int inport = m_permu_buf[i].second;
        flit *t_flit = m_permu_buf[i].first;
        //cout<<"SA, is gold state "<<t_flit->is_gold_state()<<"inport id "<<inport<<m_input_unit[inport]->get_direction()<<endl;
        DPRINTF(RubyNetwork, "PERMUTATION SwitchAllocator at Router %d "
                                     " at inport %d to flit %s at "
                                     "time: %lld\n",
                        m_router->get_id(),
                        m_router->getPortDirectionName(
                            m_input_unit[inport]->get_direction()),
                            *t_flit,
                        m_router->curCycle());
    }

    //split flit type  
    for(int i = 0; i < m_permu_buf.size(); i++){
        int inport = m_permu_buf[i].second;
        //flit *t_flit = m_permu_buf[i].first;
        
        if(m_input_unit[inport]->get_direction() == "Local")
            local_flits.push_back(m_permu_buf[i]);
        else
            order_flits.push_back(m_permu_buf[i]);
    }
    
    //sort non-local state in decend order
    sort(order_flits.begin(), order_flits.end(), std::bind(&SwitchAllocator::compareFlitID, this, std::placeholders::_1, std::placeholders::_2));

    //alloc non-local flit 
    for(int i = 0; i < order_flits.size(); i++){
        int inport = order_flits[i].second;

        //one possible case for local flit to be gold
        //Local to Local
        //in this case, release them only if the local outport is avaliable, otherwise stall at local
        int invc = order_flits[i].first->get_vc();
        int prefer_outport = m_input_unit[inport]->get_outport(invc);
        int rand_outport = -1;
                
        if(outport_ava[prefer_outport]){
            outport_ava[prefer_outport] = false;
            m_input_unit[inport]->set_flag(true);
            m_input_unit[inport]->grant_outport(invc, prefer_outport);
        }else{
            rand_outport = getANonLocalOutport();
            assert(rand_outport != -1 && m_output_unit[rand_outport]->get_direction() != "Local");
            outport_ava[rand_outport] = false;
            m_input_unit[inport]->set_flag(true);
            m_input_unit[inport]->grant_outport(invc, rand_outport);                
                //cout<<"SA: Router "<<m_router->get_id()<<" inport "<<inport<<", flit "
                //  <<gold_flits[i].first->get_type()<<", prefer "<<prefer_outport<<", deflect to "<<rand_outport<<endl;
        }
    }

    //alloc local flit
    //allow non-avaliable outport
    for(int i = 0; i < local_flits.size(); i++){
        int inport = local_flits[i].second;
        assert(m_input_unit[inport]->get_direction() == "Local");
        int invc = local_flits[i].first->get_vc();
        int prefer_outport = m_input_unit[inport]->get_outport(invc);
        int rand_outport = -1;
        if(outport_ava[prefer_outport]){
            outport_ava[prefer_outport] = false;
            m_input_unit[inport]->set_flag(true);
            m_input_unit[inport]->grant_outport(invc, prefer_outport);
        }else{
            rand_outport = getANonLocalOutport();
            if(rand_outport == -1){
                m_input_unit[inport]->set_flag(false);
                m_input_unit[inport]->grant_outport(invc, prefer_outport);
            }else{
                outport_ava[rand_outport] = false;
                m_input_unit[inport]->set_flag(true);
                assert(m_output_unit[rand_outport]->get_direction() != "Local");
                m_input_unit[inport]->grant_outport(invc, rand_outport);
                //cout<<"SA: Router "<<m_router->get_id()<<" inport "<<inport<<", flit "
                //  <<local_flits[i].first->get_type()<<", prefer "<<prefer_outport<<", deflect to "<<rand_outport<<endl;
 
            }
        }
    }

}
*/

/*
 * SA-II (or SA-o) loops through all output ports,
 * and selects one input VC (that placed a request during SA-I)
 * as the winner for this output port in a round robin manner.
 *      - For HEAD/HEAD_TAIL flits, performs simplified outvc allocation.
 *        (i.e., select a free VC from the output port).
 *      - For BODY/TAIL flits, decrement a credit in the output vc.
 * The winning flit is read out from the input VC and sent to the
 * CrossbarSwitch.
 * An increment_credit signal is sent from the InputUnit
 * to the upstream router. For HEAD_TAIL/TAIL flits, is_free_signal in the
 * credit is set to true.
 */

void
SwitchAllocator::arbitrate_outports()
{
    // Now there are a set of input vc requests for output vcs.
    // Again do round robin arbitration on these requests
    // Independent arbiter at each output port
    
    //int num_ports_req[6]={0};
    
    for (int i = 0; i < m_num_outports; i++) {
        num_ports_req[i] = 0;
    }
    //count number of requirement from local inport to outport i
    for (int i = 0; i < m_num_outports; i++){
        num_local_req[i] = 0;
    }

    RoutingAlgorithm routing_algo =
        (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();

    /*Assert check logic begins to check for multi outport assignment*/
    
    if(routing_algo == DEFLECTION_ || routing_algo == TDM_){
      bool is_prev_local;
    
        for (int outport = 0; outport < m_num_outports; outport++) {
            
            is_prev_local = false;

            for (int inport_iter = 0; inport_iter < m_num_inports;
                     inport_iter++) {

                // inport has a request this cycle for outport
                if (m_port_requests[outport][inport_iter]) {
                
    
                    ++num_ports_req[outport];   
                    if(num_ports_req[outport] >= 2){
                        //std::cout <<"\n\nis pre local" <<is_prev_local << "is local" << m_router->router_inport_id2dirn(inport_iter)<<endl;
                        //std::cout <<"router id is "<<m_router->get_id()<<endl;
                        //std::cout <<"current outport id is "<< outport << "outport direction is "<< m_router->router_outport_id2dirn(outport)<<endl;
                        assert(is_prev_local || (m_router->router_inport_id2dirn(inport_iter) == "Local")); //similar problem here
                        if(m_router->router_inport_id2dirn(inport_iter) == "Local")
                            num_local_req[outport]++;
                    }
                    else{
                        if(m_router->router_inport_id2dirn(inport_iter) == "Local"){
                            is_prev_local = true;
                            num_local_req[outport]++;                   
                        }
                    }
                }
            }
        }
    }
    //initialize, inport observed situation
    fill(inport_observed.begin(), inport_observed.end(), false);
    /*Assertion check logic ends for Multi outport assignment*/

    for (int outport = 0; outport < m_num_outports; outport++) {
        int inport = m_round_robin_inport[outport];

        for (int inport_iter = 0; inport_iter < m_num_inports;
                 inport_iter++) {
            // inport has a request this cycle for outport
            if (m_port_requests[outport][inport]) {

                if(routing_algo == DEFLECTION_ || routing_algo == TDM_){// release non-local flits first

                    if(num_ports_req[outport] >= 2){
                        //all outport request may come from local
                        //apply num request from local to prevent deadlock
                        assert(num_local_req[outport] <= num_ports_req[outport]);
                        if(num_local_req[outport] < num_ports_req[outport]){
                            if(m_router->router_inport_id2dirn(inport) == "Local"){
                                //std::cout<<"\nSA logic Local check inport value is"<<inport <<endl;
                                inport++; // if this inport is Local, skip
                                if (inport >= m_num_inports)
                                    inport = 0;

                                continue;
                            }
                        }
                    }
                
                }

                //added for deflection
                //every cycle one inport only can release one flit
                //this only happened, when flits from local
                /*
                if(routing_algo == DEFLECTION_ || routing_algo == TDM_){
                    if(inport_observed[inport] == true){
                        assert(m_input_unit[inport]->get_direction() == "Local");
                        inport++; 
                        if (inport >= m_num_inports)
                            inport = 0;
                        continue;
                    }
                }
                */

                // grant this outport to this inport
                int invc = m_vc_winners[outport][inport];

                int outvc = m_input_unit[inport]->get_outvc(invc);
                if (outvc == -1) {
                    // VC Allocation - select any free VC from outport
                    outvc = vc_allocate(outport, inport, invc);
                }

                if(routing_algo == DEFLECTION_ || routing_algo == TDM_)
                    assert(invc == outvc);

                // remove flit from Input VC
                flit *t_flit = m_input_unit[inport]->getTopFlit(invc);

                //make inport as observed
                inport_observed[inport] = true;
                //std::cout<<"\nSA select inport "<<inport<<"direction is "<< m_router->router_inport_id2dirn(inport) <<"flit from "<<invc<<endl;
                //std::cout<<"SA correspoding outport is "<<outport<<endl; 
                DPRINTF(RubyNetwork, "SwitchAllocator at Router %d "
                                     "granted outvc %d at outport %d "
                                     "to invc %d at inport %d to flit %s at "
                                     "time: %lld\n",
                        m_router->get_id(), outvc,
                        m_router->getPortDirectionName(
                            m_output_unit[outport]->get_direction()),
                        invc,
                        m_router->getPortDirectionName(
                            m_input_unit[inport]->get_direction()),
                            *t_flit,
                        m_router->curCycle());


                // Update outport field in the flit since this is
                // used by CrossbarSwitch code to send it out of
                // correct outport.
                // Note: post route compute in InputUnit,
                // outport is updated in VC, but not in flit
                t_flit->set_outport(outport);

                // set outvc (i.e., invc for next hop) in flit
                // (This was updated in VC by vc_allocate, but not in flit)
                t_flit->set_vc(outvc);

                // decrement credit in outvc
                m_output_unit[outport]->decrement_credit(outvc);
                //std::cout<<"SA, at router "<< m_router->get_id() << " selected outport is "<<outport<< "current credit is "<<m_output_unit[outport]->get_credit_count(outvc)<<"out vc is "<<outvc<<endl;

                //check next router id

                // flit ready for Switch Traversal
                t_flit->advance_stage(ST_, m_router->curCycle());
                m_router->grant_switch(inport, t_flit);
                m_output_arbiter_activity++;

                if(routing_algo == DEFLECTION_ || routing_algo == TDM_){
                    //input VC should always be empty, because it only handles 1 flit each time
                    if(m_input_unit[inport]->get_direction() != "Local"){
                        assert(!(m_input_unit[inport]->isReady(invc,
                                m_router->curCycle())));
                        m_input_unit[inport]->set_vc_idle(invc,
                                m_router->curCycle());
                        m_input_unit[inport]->increment_credit(invc, true,
                                m_router->curCycle());
                        //cout<<"flit id is "<<t_flit->getClkId()<<" inport id is "<<inport
                        //           <<" invc is "<<t_flit->get_vc()
                        //           <<" set outport is "<<t_flit->get_outport()<<endl;
                    }else{
                        assert(m_input_unit[inport]->get_direction() == "Local");
                        if ((t_flit->get_type() == TAIL_) ||
                            t_flit->get_type() == HEAD_TAIL_) {
                            //TODO:unnecessary to ensure empty
                            //can process the next one in buffer
                            // This Input VC should now be empty
                            //assert(!(m_input_unit[inport]->isReady(invc,
                            //    m_router->curCycle())));

                            // Free this VC
                            m_input_unit[inport]->set_vc_idle(invc,
                                m_router->curCycle());

                            // Send a credit back
                            // along with the information that this VC is now idle
                            m_input_unit[inport]->increment_credit(invc, true,
                                m_router->curCycle());
                            // if(m_router->get_id()==9){
                            //     cout<<"SA Free Router "<<m_router->get_id()<<" inport "<< m_input_unit[inport]->get_direction() <<", id is "<<inport<<", invc is "<<invc<<", at cycle "<<m_router->curCycle()<<endl;
                            // }
                        } else {
                            // Send a credit back
                            // but do not indicate that the VC is idle
                            m_input_unit[inport]->increment_credit(invc, false,
                                m_router->curCycle());
                        }
                    }

                }else{
                    if ((t_flit->get_type() == TAIL_) ||
                        t_flit->get_type() == HEAD_TAIL_) {

                        // This Input VC should now be empty
                        assert(!(m_input_unit[inport]->isReady(invc,
                            m_router->curCycle())));

                        // Free this VC
                        m_input_unit[inport]->set_vc_idle(invc,
                            m_router->curCycle());

                        // Send a credit back
                        // along with the information that this VC is now idle
                        m_input_unit[inport]->increment_credit(invc, true,
                            m_router->curCycle());
                    } else {
                        // Send a credit back
                        // but do not indicate that the VC is idle
                        m_input_unit[inport]->increment_credit(invc, false,
                            m_router->curCycle());
                    }
                }

                //original version
                /*
                if ((t_flit->get_type() == TAIL_) ||
                    t_flit->get_type() == HEAD_TAIL_) {

                    // This Input VC should now be empty
                    assert(!(m_input_unit[inport]->isReady(invc,
                        m_router->curCycle())));

                    // Free this VC
                    m_input_unit[inport]->set_vc_idle(invc,
                        m_router->curCycle());

                    // Send a credit back
                    // along with the information that this VC is now idle
                    m_input_unit[inport]->increment_credit(invc, true,
                        m_router->curCycle());
                    //std::cout<<"SA, check logic flag here ==================="<<"inport is "<< inport<<endl;
                    
                } else {
                    // Send a credit back
                    // but do not indicate that the VC is idle
                    if(routing_algo == DEFLECTION_ || routing_algo == TDM_)
                        panic("current flit type is not a HEAD_TAIL_ flit");
                    m_input_unit[inport]->increment_credit(invc, false,
                        m_router->curCycle());
                }
                */
                //Verify that flits leave the same cycle they arrive
                if(routing_algo == DEFLECTION_ || routing_algo == TDM_){
                    //cout<<"SA: "<<"router id "<<m_router->get_id()<<", inport direction "<<m_input_unit[inport]->get_direction()<<", flit time "<<t_flit->get_time()<<", router time "<<m_router->curCycle()<<endl;
                    assert(t_flit->get_time() == m_router->curCycle() || m_input_unit[inport]->get_direction() == "Local"); 
                }
                

                // remove this request
                m_port_requests[outport][inport] = false;

                // Update Round Robin pointer
                m_round_robin_inport[outport] = inport + 1;
                if (m_round_robin_inport[outport] >= m_num_inports)
                    m_round_robin_inport[outport] = 0;

                break; // got a input winner for this outport
            }

            inport++;
            if (inport >= m_num_inports)
                inport = 0;
        }
    }
}

/*
 * A flit can be sent only if
 * (1) there is at least one free output VC at the
 *     output port (for HEAD/HEAD_TAIL),
 *  or
 * (2) if there is at least one credit (i.e., buffer slot)
 *     within the VC for BODY/TAIL flits of multi-flit packets.
 * and
 * (3) pt-to-pt ordering is not violated in ordered vnets, i.e.,
 *     there should be no other flit in this input port
 *     within an ordered vnet
 *     that arrived before this flit and is requesting the same output port.
 * (4) whether it is the right wave to release flit from input unit
 */

bool
SwitchAllocator::send_allowed(int inport, int invc, int outport, int outvc)
{
    PortDirection inport_dirn  = m_input_unit[inport]->get_direction();
    PortDirection outport_dirn = m_output_unit[outport]->get_direction();
    RouteInfo route = m_input_unit[inport]->peekTopFlit(invc)->get_route();
    // Check if outvc needed
    // Check if credit needed (for multi-flit packet)
    // Check if ordering violated (in ordered vnet)

    int vnet = get_vnet(invc);
    bool has_outvc = (outvc != -1);
    bool has_credit = false;

    RoutingAlgorithm routing_algo =
        (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();
    
    if (!has_outvc) {

        // needs outvc
        // this is only true for HEAD and HEAD_TAIL flits.
        //Original, TODO,if (m_output_unit[outport]->has_free_vc(vnet)) {
        if (m_output_unit[outport]->has_free_vc(vnet, invc,
                                    inport_dirn, outport_dirn, route)) {
            
            has_outvc = true;

            // each VC has at least one buffer,
            // so no need for additional credit check
            has_credit = true;
        }
    } else {
        if(routing_algo != DEFLECTION_ && routing_algo != TDM_)
            has_credit = m_output_unit[outport]->has_credit(outvc);
        else{
            // when the TAIL flit from "Local" arrive at SA stage, the "HEAD" flit at the downstream router
            // already finish the increment credit behavior at the nxt_SA stage, which will set this vc as IDLE
            // In deflection model, should ignore the vc statement, but make sure each flit leaves immediately
            has_credit = true;
        }
    }

    // cannot send if no outvc or no credit.
    if (!has_outvc || !has_credit)
        return false;


    // protocol ordering check
    if ((m_router->get_net_ptr())->isVNetOrdered(vnet)) {

        // enqueue time of this flit
        Cycles t_enqueue_time = m_input_unit[inport]->get_enqueue_time(invc);

        // check if any other flit is ready for SA and for same output port
        // and was enqueued before this flit
        int vc_base = vnet*m_vc_per_vnet;
        for (int vc_offset = 0; vc_offset < m_vc_per_vnet; vc_offset++) {
            int temp_vc = vc_base + vc_offset;
            if (m_input_unit[inport]->need_stage(temp_vc, SA_,
                                                 m_router->curCycle()) &&
               (m_input_unit[inport]->get_outport(temp_vc) == outport) &&
               (m_input_unit[inport]->get_enqueue_time(temp_vc) <
                    t_enqueue_time)) {
                return false;
            }
        }
    }

    // if(inport == 0)
    //     inport = 1; //there is no reason, routing unit always return 1 for local inport
    //TODO, local inport may not only 1 and 0, ther emay be multiple
    
    if(routing_algo == TDM_){
        //hasn't find a outport with avaliable next wave
        //only for Local inport
        // if(inport == 1 || inport == 0){
        //     if(m_input_unit[0]->get_flag() == false && m_input_unit[1]->get_flag() == false)
        //         return false;
        // }else{
        //local inport number may vary based on # of l1&l2 cache
        if(m_input_unit[inport]->get_flag() == false)
            return false;
        //}
    }
    

    return true;
}

// Assign a free VC to the winner of the output port.
int
SwitchAllocator::vc_allocate(int outport, int inport, int invc)
{
    // ICN Lab 3:
    // Hint: invc, route, inport_dirn, outport_dirn are provided
    // to implement escape VC
    PortDirection inport_dirn  = m_input_unit[inport]->get_direction();
    PortDirection outport_dirn = m_output_unit[outport]->get_direction();
    RouteInfo route = m_input_unit[inport]->peekTopFlit(invc)->get_route();
    
    // Select a free VC from the output port
    int vnet = get_vnet(invc);
    int outvc = m_output_unit[outport]->select_free_vc(vnet, invc,
                                        inport_dirn, outport_dirn, route);//Original, TODO,int outvc = m_output_unit[outport]->select_free_vc(get_vnet(invc));

    // has to get a valid VC since it checked before performing SA
    assert(outvc != -1);
    m_input_unit[inport]->grant_outvc(invc, outvc);
    return outvc;
}

// Wakeup the router next cycle to perform SA again
// if there are flits ready.
void
SwitchAllocator::check_for_wakeup()
{
    Cycles nextCycle = m_router->curCycle() + Cycles(1);

    for (int i = 0; i < m_num_inports; i++) {
        for (int j = 0; j < m_num_vcs; j++) {
            if (m_input_unit[i]->need_stage(j, SA_, nextCycle)) {
                m_router->schedule_wakeup(Cycles(1));
                return;
            }
        }
    }
}

int
SwitchAllocator::get_vnet(int invc)
{
    int vnet = invc/m_vc_per_vnet;
    assert(vnet < m_router->get_num_vnets());
    return vnet;
}


// Clear the request vector within the allocator at end of SA-II.
// Was populated by SA-I.
void
SwitchAllocator::clear_request_vector()
{
    for (int i = 0; i < m_num_outports; i++) {
        for (int j = 0; j < m_num_inports; j++) {
            m_port_requests[i][j] = false;
        }
    }
}

void
SwitchAllocator::resetStats()
{
    m_input_arbiter_activity = 0;
    m_output_arbiter_activity = 0;
}

void
SwitchAllocator::verify_VCs_empty(){

    //is_invc_idle checks only for VC0 of eahc vnet

    for(int inport=0; inport < m_num_inports; ++inport){

        //A + A'B = A + B;
        //Idle || Idle'.Local = Idle || Local;
        //the index of vc of each vnet is vnet its self, if the num-vc-per-vnet == 1
        assert(m_input_unit[inport]->is_invc0_idle() || (m_input_unit[inport]->get_direction() == "Local"));
    }

}
