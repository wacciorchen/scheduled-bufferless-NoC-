README for Garnet2.0
Written By: Tushar Krishna (tushar@ece.gatech.edu)
Last Updated: Jul 9, 2016
Scheduled bufferless NoC
Written by Chen Chen
Last Update: Nov 20, 2019
-------------------------------------------------------

Garnet Network Parameters and Setup:
- GarnetNetwork.py
    * defaults can be overwritten from command line (see configs/network/Network.py)
- GarnetNetwork.hh/cc
    * sets up the routers and links
    * collects stats


CODE FLOW
- NetworkInterface.cc::wakeup()
    * Every NI connected to one coherence protocol controller on one end, and one router on the other.
    * receives messages from coherence protocol buffer in appropriate vnet and converts them into network packets and sends them into the network.
        * garnet2.0 adds the ability to capture a network trace at this point.
    * receives flits from the network, extracts the protocol message and sends it to the coherence protocol buffer in appropriate vnet.
    * manages flow-control (i.e., credits) with its attached router.
    * The consuming flit/credit output link of the NI is put in the global event queue with a timestamp set to next cycle.
      The eventqueue calls the wakeup function in the consumer.

- NetworkLink.cc::wakeup()
    * receives flits from NI/router and sends it to NI/router after m_latency cycles delay
        * Default latency value for every link can be set from command line (see configs/network/Network.py)
        * Per link latency can be overwritten in the topology file
    * The consumer of the link (NI/router) is put in the global event queue with a timestamp set after m_latency cycles.
      The eventqueue calls the wakeup function in the consumer.

- Router.cc::wakeup()
    * Loop through all InputUnits and call their wakeup()
    * Loop through all OutputUnits and call their wakeup()
    * Call SwitchAllocator's wakeup()
    * Call CrossbarSwitch's wakeup()
    * The router's wakeup function is called whenever any of its modules (InputUnit, OutputUnit, SwitchAllocator, CrossbarSwitch) have
      a ready flit/credit to act upon this cycle.

- InputUnit.cc::wakeup()
    * Read input flit from upstream router if it is ready for this cycle
    * For HEAD/HEAD_TAIL flits, perform route computation, and update route in the VC.
    * Buffer the flit for (m_latency - 1) cycles and mark it valid for SwitchAllocation starting that cycle.
        * Default latency for every router can be set from command line (see configs/network/Network.py)
        * Per router latency (i.e., num pipeline stages) can be set in the topology file

- OutputUnit.cc::wakeup()
    * Read input credit from downstream router if it is ready for this cycle
    * Increment the credit in the appropriate output VC state.
    * Mark output VC as free if the credit carries is_free_signal as true

- SwitchAllocator.cc::wakeup()
    * Note: SwitchAllocator performs VC arbitration and selection within it.
    * SA-I (or SA-i): Loop through all input VCs at every input port, and select one in a round robin manner.
        * For HEAD/HEAD_TAIL flits only select an input VC whose output port has at least one free output VC.
        * For BODY/TAIL flits, only select an input VC that has credits in its output VC.
    * Place a request for the output port from this VC.
    * SA-II (or SA-o): Loop through all output ports, and select one input VC (that placed a request during SA-I) as the winner for this output port in a round robin manner.
        * For HEAD/HEAD_TAIL flits, perform outvc allocation (i.e., select a free VC from the output port).
        * For BODY/TAIL flits, decrement a credit in the output vc.
    * Read the flit out from the input VC, and send it to the CrossbarSwitch
    * Send a increment_credit signal to the upstream router for this input VC.
        * for HEAD_TAIL/TAIL flits, mark is_free_signal as true in the credit.
        * The input unit sends the credit out on the credit link to the upstream router.
    * Reschedule the Router to wakeup next cycle for any flits ready for SA next cycle.

- CrossbarSwitch.cc::wakeup()
    * Loop through all input ports, and send the winning flit out of its output port onto the output link.
    * The consuming flit output link of the router is put in the global event queue with a timestamp set to next cycle.
      The eventqueue calls the wakeup function in the consumer.

Scheduled bufferless NoC note by Chen Chen:

-sim/clock_object.hh
    *add wave and wave number to implement TDM function
    *call curWave() function to get the current wave.

-GarnetNetwork.cc
    *modify weight of "Local" link to -1 to identify the destination


add wave : 
    - modify Routing unit.cc/hh, apply loopuptable similar function, since assume receiver buffer has infinite space
    - Router::addOutport
    - GarnetNetwork.cc:: make internal link
    - Basiclink.cc/hh : add m_wave
    - Basiclink.py: add wave port    

check: switchAllocator.cc/hh for specific routing_algo case
confused: network interface, time to release flit.  // assume it is after routing algorithm, figure out the logic flow 
            -possible solution: get router_id from flit;
                                assume networkinterface wake up after router, so each router already access link_wave info
                                check link_wave then

-access current time:
    1. flit->m_time, flit.cc/hh
    2. core.hh curTick(); universal time
    
0729: modify routingunit.cc L:582 & Ni.cc L:584 logic that prevent system check all output port
        Ni:L 583 link 2->6 is the output port of each router & routingunit.cc L:588, 0,1 are for Local?
        --rtunit.cc::addWave, ensure link start at 2
        --add operator "==" in type.h for Cycle compare    

      port to id in rutingunit: 0,1 Local; 2 North; 3 West; 4 East; 5 South (related with weight?)
      TODO: change the routing table traversal range in multiple function  
    pending: --SA :TODO
            --inputunit.cc m_id / inport for?

0731: in sconfig/topology/ "src_outport" & "dest_inport" means the position of router's outport, not the direction of a link 

0801: in OutVcState.cc change max credit to be "1", so that every time one flit can pass through the router.
        garnetNetwork make extLink, link latency -> 0

	TODO: add adjcent router to router.hh, when router SA determine the outport, it may need to notify its adjacent router to do sth corresponding to this action so that Ni will know what time to release the flit.


0813: Topology.cc :1. determine Port direction. If still problem, can look at there.
                   2. for disconnected link, need to manually set weight to inf?
                   3. weight only record adjacent link weight, use dist and weight to represent the minimal path 
0815: remember every time update wave, update wave number in clock_object.hh also

0819: ignore ruby/system/Sequencer.cc deadlock panic. Or change dead_lock threashold

0830: fluminate no check point; streamcluster: no exit
	vips weired; fasim:exit quick
 
 
 //packet truncation:
 in OutVcState.cc:
 decrement: comment out the assert();
 This makes the credit has possibility to go negative: when set idle, reset credit also.
