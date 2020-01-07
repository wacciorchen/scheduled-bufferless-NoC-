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


#include "mem/ruby/network/garnet2.0/RoutingUnit.hh"

#include "base/cast.hh"
#include "base/logging.hh"
#include "mem/ruby/network/garnet2.0/InputUnit.hh"
#include "mem/ruby/network/garnet2.0/Router.hh"
#include "mem/ruby/slicc_interface/Message.hh"

RoutingUnit::RoutingUnit(Router *router)
{
    m_router = router;
    m_routing_table.clear();
    m_weight_table.clear();
    m_wave_table.clear();

    // while(m_wave_table.size() < 6){
    //     std::vector<int> tmp_wave_table;
    //     m_wave_table.push_back(tmp_wave_table);
    // }

    m_wave_table.resize(10);
    //std::cout<<"ruunit::mwavetable size is "<<m_wave_table.size()<<endl;
}

void
RoutingUnit::addRoute(const NetDest& routing_table_entry)
{
    m_routing_table.push_back(routing_table_entry);
}

void
RoutingUnit::addWeight(int link_weight)
{
    m_weight_table.push_back(link_weight);
}

void
RoutingUnit::addWave(const std::vector<int>& link_wave, int outport)
{

    std:: cout <<"\n================addWave in rot unit section ==============="<<endl;
    std:: cout << "ROuter id "<< m_router->get_id() << "outport position" << outport <<endl;
    std::cout <<"wave add in: ";    
    std::cout << "size of this ourport wave is "<<link_wave.size()<<endl;
    if(outport > 9){
        panic("size of wave table is not enough, increase in RoutingUnit constractor");
    }
    m_wave_table[outport].clear();
    for(int waveNum = 0; waveNum < link_wave.size(); waveNum++){
        m_wave_table[outport].push_back(link_wave[waveNum]);
        std::cout << link_wave[waveNum]<<" ";
    }
    std::cout <<"\n=====================current link end===================="<<endl;


}

/*
 * This is the default routing algorithm in garnet.
 * The routing table is populated during topology creation.
 * Routes can be biased via weight assignments in the topology file.
 * Correct weight assignments are critical to provide deadlock avoidance.
 */

int
RoutingUnit::lookupRoutingTable(int vnet, NetDest msg_destination)
{
    // First find all possible output link candidates
    // For ordered vnet, just choose the first
    // (to make sure different packets don't choose different routes)
    // For unordered vnet, randomly choose any of the links
    // To have a strict ordering between links, they should be given
    // different weights in the topology file

    int output_link = -1;
    int min_weight = INFINITE_;
    std::vector<int> output_link_candidates;
    int num_candidates = 0;

    // Identify the minimum weight among the candidate output links
    for (int link = 0; link < m_routing_table.size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(m_routing_table[link])) {
            //std::cout<<"Router "<<m_router->get_id()<<" has intersection "<<endl;
            if (m_weight_table[link] <= min_weight)
                min_weight = m_weight_table[link];
        }
    }

    for (int link = 0; link < m_routing_table.size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(m_routing_table[link])) {
            if (m_weight_table[link] == min_weight) {
                //std::cout << link <<" ";
                num_candidates++;
                output_link_candidates.push_back(link);
            }
        }
    }

    if (output_link_candidates.size() == 0) {
        std::cout<<"Router "<<m_router->get_id();
        fatal("Fatal Error:: No Route exists from this Router.");
        exit(0);
    }

    // Randomly select any candidate output link
    int candidate = 0;
    if (!(m_router->get_net_ptr())->isVNetOrdered(vnet))
        candidate = rand() % num_candidates;

    output_link = output_link_candidates.at(candidate);
    return output_link;
}


void
RoutingUnit::addInDirection(PortDirection inport_dirn, int inport_idx)
{
    m_inports_dirn2idx[inport_dirn] = inport_idx;
    m_inports_idx2dirn[inport_idx]  = inport_dirn;
}

void
RoutingUnit::addOutDirection(PortDirection outport_dirn, int outport_idx)
{
    //std::cout<<"\n\nrtunit, outdirection setup outport_dirn outport_idx "<< outport_dirn <<" "<< outport_idx<<endl;
    m_outports_dirn2idx[outport_dirn] = outport_idx;
    m_outports_idx2dirn[outport_idx]  = outport_dirn;
}

// outportCompute() is called by the InputUnit
// It calls the routing table by default.
// A template for adaptive topology-specific routing algorithm
// implementations using port directions rather than a static routing
// table is provided here.

int
RoutingUnit::outportCompute(RouteInfo route, int inport,
                            PortDirection inport_dirn)
{
    int outport = -1;

    // Routing Algorithm set in GarnetNetwork.py
    // Can be over-ridden from command line using --routing-algorithm = 1
    RoutingAlgorithm routing_algorithm =
        (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();

    if (route.dest_router == m_router->get_id()) {

        // Multiple NIs may be connected to this router,
        // all with output port direction = "Local"
        // Get exact outport id from table
        outport = lookupRoutingTable(route.vnet, route.net_dest);
        
        //To prevent multiple flits attempting to exit from Local
        if(routing_algorithm != DEFLECTION_ && routing_algorithm != TDM_)    
            return outport;
    }

    switch (routing_algorithm) {
        case TABLE_:  outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
        case XY_:     outport =
            outportComputeXY(route, inport, inport_dirn); break;
        case TURN_MODEL_: outport =
            outportComputeTurnModel(route, inport, inport_dirn); break;
        case RANDOM_: outport =
            outportComputeRandom(route, inport, inport_dirn); break;
        case DEFLECTION_: outport = 
            outportComputeDeflection(route, inport, inport_dirn); break;
        // any custom algorithm
        case CUSTOM_: outport =
            outportComputeCustom(route, inport, inport_dirn); break;
        case TDM_: outport = 
            outportComputeTDM(route.vnet, route.net_dest, inport_dirn, inport); break;
        default: outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
    }

    assert(outport != -1);
    return outport;
}

//ANK modification begins
//outportCompute function with addition invc paramteter for escape VC restrictive routing
int
RoutingUnit::outportCompute(RouteInfo route, int inport,
                            PortDirection inport_dirn, int invc)
{
    int outport = -1;

  RoutingAlgorithm routing_algorithm =
        (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();


    if (route.dest_router == m_router->get_id()) {

        // Multiple NIs may be connected to this router,
        // all with output port direction = "Local"
        // Get exact outport id from table
        outport = lookupRoutingTable(route.vnet, route.net_dest);
        if(routing_algorithm != DEFLECTION_ && routing_algorithm != TDM_)
            return outport;
    }

    // Routing Algorithm set in GarnetNetwork.py
    // Can be over-ridden from command line using --routing-algorithm = 1
      switch (routing_algorithm) {
        case TABLE_:  outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
        case XY_:     outport =
            outportComputeXY(route, inport, inport_dirn); break;
        case TURN_MODEL_: outport =
            outportComputeTurnModel(route, inport, inport_dirn); break;
        case RANDOM_: //Modification
            if( (invc % m_router->get_vc_per_vnet() == EVC))
                outport = outportComputeXY(route, inport, inport_dirn);
            else{ 
                outport = outportComputeRandom(route, inport, inport_dirn);} break;
        // any custom algorithm
        case CUSTOM_: outport =
            outportComputeCustom(route, inport, inport_dirn); break;
        case DEFLECTION_: outport = 
            outportComputeDeflection(route, inport, inport_dirn); break;
        case TDM_: outport = 
            outportComputeTDM(route.vnet, route.net_dest, inport_dirn, inport); break;    
        default: outport =
            lookupRoutingTable(route.vnet, route.net_dest); break;
    }

    assert(outport != -1);
    return outport;
}
//TDM
// int
// RoutingUnit::outportCompute(RouteInfo route, int inport,
//                             PortDirection inport_dirn, int invc, Cycles cur_cycle)
// {
//     int outport = -1;

//   RoutingAlgorithm routing_algorithm =
//         (RoutingAlgorithm) m_router->get_net_ptr()->getRoutingAlgorithm();


//     if (route.dest_router == m_router->get_id()) {

//         // Multiple NIs may be connected to this router,
//         // all with output port direction = "Local"
//         // Get exact outport id from table
//         outport = lookupRoutingTable(route.vnet, route.net_dest);
//         if(routing_algorithm != DEFLECTION_)
//             return outport;
//     }

//     // Routing Algorithm set in GarnetNetwork.py
//     // Can be over-ridden from command line using --routing-algorithm = 1
//       switch (routing_algorithm) {
//         case TABLE_:  outport =
//             lookupRoutingTable(route.vnet, route.net_dest); break;
//         case XY_:     outport =
//             outportComputeXY(route, inport, inport_dirn); break;
//         case TURN_MODEL_: outport =
//             outportComputeTurnModel(route, inport, inport_dirn); break;
//         case RANDOM_: //Modification
//             if( (invc % m_router->get_vc_per_vnet() == EVC))
//                 outport = outportComputeXY(route, inport, inport_dirn);
//             else{ 
//                 outport = outportComputeRandom(route, inport, inport_dirn);} break;
//         // any custom algorithm
//         case CUSTOM_: outport =
//             outportComputeCustom(route, inport, inport_dirn); break;
//         case DEFLECTION_: outport = 
//             outportComputeDeflection(route, inport, inport_dirn); break;
//         case TDM_: outport = 
//             outportComputeTDM(route.vnet, route.net_dest); break;    
//         default: outport =
//             lookupRoutingTable(route.vnet, route.net_dest); break;
//     }

//     assert(outport != -1);
//     return outport;
// }


//ANK modification ends

// XY routing implemented using port directions
// Only for reference purpose in a Mesh
// By default Garnet uses the routing table
int
RoutingUnit::outportComputeXY(RouteInfo route,
                              int inport,
                              PortDirection inport_dirn)
{
    PortDirection outport_dirn = "Unknown";

    int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
    int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

    int dest_id = route.dest_router;
    int dest_x = dest_id % num_cols;
    int dest_y = dest_id / num_cols;

    int x_hops = abs(dest_x - my_x);
    int y_hops = abs(dest_y - my_y);

    bool x_dirn = (dest_x >= my_x);
    bool y_dirn = (dest_y >= my_y);

    // already checked that in outportCompute() function
    assert(!(x_hops == 0 && y_hops == 0));

    if (x_hops > 0) {
        if (x_dirn) {
            assert(inport_dirn == "Local" || inport_dirn == "West");
            outport_dirn = "East";
        } else {
            assert(inport_dirn == "Local" || inport_dirn == "East");
            outport_dirn = "West";
        }
    } else if (y_hops > 0) {
        if (y_dirn) {
            // "Local" or "South" or "West" or "East"
            assert(inport_dirn != "North");
            outport_dirn = "North";
        } else {
            // "Local" or "North" or "West" or "East"
            assert(inport_dirn != "South");
            outport_dirn = "South";
        }
    } else {
        // x_hops == 0 and y_hops == 0
        // this is not possible
        // already checked that in outportCompute() function
        panic("x_hops == y_hops == 0");
    }

    return m_outports_dirn2idx[outport_dirn];
}


int
RoutingUnit::outportComputeDeflection(RouteInfo route,
                              int inport,
                              PortDirection inport_dirn)
{
    //Deflection Code here
    //Find Desirable direction, if no other flit in the router is already
    //allocated that port, then proceed, otherwise misroute


    //int preferred_outport = outportComputeRandom(route, inport, inport_dirn);
    int preferred_outport = lookupRoutingTable(route.vnet, route.net_dest); // use this funx to find the destination, the weight to local is always 1
    //int obtained = m_router->getAllocatedDirection(preferred_outport, inport_dirn, inport);
    
    

    //assert(((m_outports_idx2dirn[obtained] == "Local") && (m_outports_idx2dirn[preferred_outport] == "Local")) || (m_outports_idx2dirn[obtained] != "Local"));
    
    //calculate in SA model
    return preferred_outport;

    //return 0; 
}


int
RoutingUnit::outportComputeTurnModel(RouteInfo route,
                                    int inport,
                                    PortDirection inport_dirn)
{

    PortDirection outport_dirn = "Unknown";

    int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
    int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

    int dest_id = route.dest_router;
    int dest_x = dest_id % num_cols;
    int dest_y = dest_id / num_cols;

    int x_hops = abs(dest_x - my_x);
    int y_hops = abs(dest_y - my_y);

    bool x_dirn = (dest_x >= my_x);
    bool y_dirn = (dest_y >= my_y);

    // already checked that in outportCompute() function
    assert(!(x_hops == 0 && y_hops == 0));

    /////////////////////////////////////////
    // ICN Lab 3: Insert code here

    //ANK modification starts

    int rand_dirn = random()%2;
        
    if(x_hops == 0){
        outport_dirn = y_dirn ? "North" : "South";      
    }   
    else if(y_hops == 0){
        outport_dirn = x_dirn ? "East" : "West";
    }
    else if(inport_dirn == "East" || inport_dirn == "West"){    //Restricting North turn
        if(y_dirn)
            outport_dirn = (inport_dirn == "East") ? "West" : "East";
        else{
            if(inport_dirn == "East")
                outport_dirn = rand_dirn ? "West" : "South";
            else
                outport_dirn = rand_dirn ? "East" : "South";
        }
    }
    else{ //North and South inports 
        if (!x_dirn && !y_dirn) // North inport and going to Quadrant III
                outport_dirn = rand_dirn ? "West" : "South";
        else if(x_dirn && !y_dirn) //North inport and going to Quadrant IV
                outport_dirn = rand_dirn ? "East" : "South";
        else    //South inport then it needs to proceed North
            outport_dirn = "North";
    }
    
    //ANK modification ends

    return m_outports_dirn2idx[outport_dirn];
}

int
RoutingUnit::outportComputeRandom(RouteInfo route,
                              int inport,
                              PortDirection inport_dirn)
{
    PortDirection outport_dirn = "Unknown";

    int M5_VAR_USED num_rows = m_router->get_net_ptr()->getNumRows();
    int num_cols = m_router->get_net_ptr()->getNumCols();
    assert(num_rows > 0 && num_cols > 0);

    int my_id = m_router->get_id();
    int my_x = my_id % num_cols;
    int my_y = my_id / num_cols;

    int dest_id = route.dest_router;
    int dest_x = dest_id % num_cols;
    int dest_y = dest_id / num_cols;

    int x_hops = abs(dest_x - my_x);
    int y_hops = abs(dest_y - my_y);

    bool x_dirn = (dest_x >= my_x);
    bool y_dirn = (dest_y >= my_y);

    // already checked that in outportCompute() function
    assert(!(x_hops == 0 && y_hops == 0));

    if (x_hops == 0)
    {
        if (y_dirn > 0)
            outport_dirn = "North";
        else
            outport_dirn = "South";
    }
    else if (y_hops == 0)
    {
        if (x_dirn > 0)
            outport_dirn = "East";
        else
            outport_dirn = "West";
    } else {
        int rand = random() % 2;

        if (x_dirn && y_dirn) // Quadrant I
            outport_dirn = rand ? "East" : "North";
        else if (!x_dirn && y_dirn) // Quadrant II
            outport_dirn = rand ? "West" : "North";
        else if (!x_dirn && !y_dirn) // Quadrant III
            outport_dirn = rand ? "West" : "South";
        else // Quadrant IV
            outport_dirn = rand ? "East" : "South";
    }

    return m_outports_dirn2idx[outport_dirn];
}

int
RoutingUnit::outportComputeTDM(int vnet, NetDest msg_destination,
								PortDirection inport_dirn, int inport)
{
    //apply lookup to get the most suitable outport
    //if it is local, then goes local
    //TODO: change weight of local back to 1
    //all link weight should be 1
    //to express the shortest path
    
    int min_weight = INFINITE_;
    std::vector<int> output_link_candidates;
    bool uTurnFlag = false;
    int num_candidates = 0;
    int output_link = -1;
    // Identify the minimum weight among the candidate output links
    for (int link = 0; link < m_routing_table.size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(m_routing_table[link])) {

        if (m_weight_table[link] <= min_weight)
            min_weight = m_weight_table[link];
        }
    }

    //std::cout<<"\nrtunit================================================"<<endl;
    //std::cout << "candi link id "; 
    for (int link = 0; link < m_routing_table.size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(m_routing_table[link])) {
            PortDirection outport_dirn = outport_id2dirn(link);
            if (m_weight_table[link] == min_weight) { // make u turn as the least priority
                if(outport_dirn != "Local"){
                    if(outport_dirn != inport_dirn){                
                        num_candidates++;
                        output_link_candidates.push_back(link);
                    }else{
                        uTurnFlag = true;
                    }
                }else{
                    num_candidates++;
                    output_link_candidates.push_back(link);
                }
           }
        }
    }

    if (output_link_candidates.size() == 0 && uTurnFlag == false) {
        fatal("Fatal Error:: No Route exists from this Router.");
        exit(0);
    }else if(output_link_candidates.size() == 0 && uTurnFlag == true){
        //only uturn has avaliable link
        int preferred_outport = outport_dirn2id(inport_dirn);
        return preferred_outport;
    }

    // Randomly select any candidate output link
    int candidate = 0;
    if (!(m_router->get_net_ptr())->isVNetOrdered(vnet))
        candidate = rand() % num_candidates;

    output_link = output_link_candidates.at(candidate);
    return output_link;

    //May have multiple outport
    //apply deflection and wave function to determine which one
    //in allocated direction
    //calculate better outport based on wave
    //int obtained = m_router->getWaveDirection(output_link_candidates, inport_dirn, inport);
    
    //bool assert_cond = ((m_outports_idx2dirn[obtained] == "Local") && (m_outports_idx2dirn[output_link_candidates[0]] == "Local")) || (m_outports_idx2dirn[obtained] != "Local");
    //std::cout << "assert_cond is " << assert_cond << "\n";
    //std::cout<<"obtained link id "<< obtained<<endl;
    //std::cout <<"\nrtunitend============================================="<<endl;
    //int preferred_outport = lookupRoutingTable(vnet, msg_destination);
    //assert(((m_outports_idx2dirn[obtained] == "Local") && (m_outports_idx2dirn[output_link_candidates[0]] == "Local")) || (m_outports_idx2dirn[obtained] != "Local"));
    //return preferred_outport;

}

// Template for implementing custom routing algorithm
// using port directions. (Example adaptive)
int
RoutingUnit::outportComputeCustom(RouteInfo route,
                                 int inport,
                                 PortDirection inport_dirn)
{
    panic("%s placeholder executed", __FUNCTION__);
}
