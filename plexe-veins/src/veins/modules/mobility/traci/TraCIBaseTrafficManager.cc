//
// Copyright (C) 2013-2018 Michele Segata <segata@ccs-labs.org>, Stefan Joerer <joerer@ccs-labs.org>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#include "veins/modules/mobility/traci/TraCIBaseTrafficManager.h"

using namespace Veins;

std::vector<nodeData> vehData;

Define_Module(TraCIBaseTrafficManager);

void TraCIBaseTrafficManager::initialize(int stage)
{

    cSimpleModule::initialize(stage);

    if (stage == 0) {

        // empty all vectors
        vehicleTypeIds.clear();
        vehiclesCount.clear();
        laneIds.clear();
        roadIds.clear();
        routeIds.clear();
        laneIdsOnEdge.clear();
        routeStartLaneIds.clear();
        vehicleInsertQueue.clear();

        insertInOrder = true;

        // search for the scenario manager. it will be needed to inject vehicles
        manager = FindModule<Veins::TraCIScenarioManager*>::findGlobalModule();
        ASSERT2(manager, "cannot find TraciScenarioManager");

        // reset vehicles counter
        vehCounter = 0;
        initScenario = false;

        // start vehicles insertion
        insertVehiclesTrigger = new cMessage("insertVehiclesTrigger");
        scheduleAt(simTime() + manager->getUpdateInterval(), insertVehiclesTrigger);
    }
}

void TraCIBaseTrafficManager::handleSelfMsg(cMessage* msg)
{

    if (msg == insertVehiclesTrigger) {

        if (manager->isTraciInitialized()) {
            insertVehicles();
        }

        scheduleAt(simTime() + manager->getUpdateInterval(), insertVehiclesTrigger);
        return;
    }
}

void TraCIBaseTrafficManager::handleMessage(cMessage* msg)
{
    if (msg->isSelfMessage()) {
        handleSelfMsg(msg);
    }
}

void TraCIBaseTrafficManager::finish()
{
    if (insertVehiclesTrigger) {
        cancelAndDelete(insertVehiclesTrigger);
        insertVehiclesTrigger = 0;
    }
}

int TraCIBaseTrafficManager::findVehicleTypeIndex(std::string vehType)
{

    unsigned int i;

    for (i = 0; i < vehicleTypeIds.size(); i++) {
        if (vehicleTypeIds[i].compare(vehType) == 0) {
            return i;
        }
    }

    return -1;
}

void TraCIBaseTrafficManager::loadSumoScenario()
{

    commandInterface = manager->getCommandInterface();

    // get all the vehicle types
    if (vehicleTypeIds.size() == 0) {
        std::list<std::string> vehTypes = commandInterface->getVehicleTypeIds();
        EV << "Having currently " << vehTypes.size() << " vehicle types" << std::endl;
        for (std::list<std::string>::const_iterator i = vehTypes.begin(); i != vehTypes.end(); ++i) {
            if (i->compare("DEFAULT_VEHTYPE") != 0) {
                EV << "found vehType " << (*i) << std::endl;
                vehicleTypeIds.push_back(*i);
                // set counter of vehicles for this vehicle type to 0
                vehiclesCount.push_back(0);
            }
        }
    }
    // get all roads
    if (roadIds.size() == 0) {
        std::list<std::string> roads = commandInterface->getRoadIds();
        EV << "Having currently " << roads.size() << " roads in the scenario" << std::endl;
        for (std::list<std::string>::const_iterator i = roads.begin(); i != roads.end(); ++i) {
            EV << *i << std::endl;
            roadIds.push_back(*i);
        }
    }
    // get all lanes
    if (laneIds.size() == 0) {
        std::list<std::string> lanes = commandInterface->getLaneIds();
        EV << "Having currently " << lanes.size() << " lanes in the scenario" << std::endl;
        for (std::list<std::string>::const_iterator i = lanes.begin(); i != lanes.end(); ++i) {
            EV << *i << std::endl;
            laneIds.push_back(*i);
            std::string edgeId = commandInterface->lane(*i).getRoadId();
            laneIdsOnEdge[edgeId].push_back(*i);
        }
    }
    // get all routes
    if (routeIds.size() == 0) {
        std::list<std::string> routes = commandInterface->getRouteIds();
        EV << "Having currently " << routes.size() << " routes in the scenario" << std::endl;
        for (std::list<std::string>::const_iterator i = routes.begin(); i != routes.end(); ++i) {
            std::string routeId = *i;
            EV << routeId << std::endl;
            routeIds.push_back(routeId);
            std::list<std::string> routeEdges = commandInterface->route(routeId).getRoadIds();
            std::string firstEdge = *(routeEdges.begin());
            EV << "First Edge of route " << routeId << " is " << firstEdge << std::endl;
            routeStartLaneIds[routeId] = laneIdsOnEdge[firstEdge];
        }
    }
    // inform inheriting classes that scenario is loaded
    scenarioLoaded();
}

void TraCIBaseTrafficManager::insertVehicles()
{

    // if not already done, load all roads, all vehicle types, etc...
    if (!initScenario) {
        loadSumoScenario();
        initScenario = true;
    }

    // insert the vehicles in the queue
    for (InsertQueue::iterator i = vehicleInsertQueue.begin(); i != vehicleInsertQueue.end(); ++i) {
        std::string route = routeIds[i->first];
        EV << "process " << route << std::endl;
        std::deque<struct Vehicle>::iterator vi = i->second.begin();
        while (vi != i->second.end() && i->second.size() != 0) {
            bool suc = false;
            struct Vehicle v = *vi;
            std::string type = vehicleTypeIds[v.id];
            std::stringstream veh;
            veh << type << "." << vehiclesCount[v.id];

            // do we need to put this vehicle on a particular lane, or can we put it on any?

            if (v.lane == -1 && !insertInOrder) {

                // try to insert that into any lane
                for (unsigned int laneId = 0; !suc && laneId < routeStartLaneIds[route].size(); laneId++) {
                    EV << "trying to add " << veh.str() << " with " << route << " vehicle type " << type << std::endl;
                    suc = commandInterface->addVehicle(veh.str(), type, route, simTime(), v.position, v.speed, laneId);
                    if (suc) break;
                }
                if (!suc) {
                    // if we did not manager to insert a car on any lane, then this route is full and we can just stop
                    // TODO: this is not true if we want to insert a vehicle not at the beginning of the route. fix this
                    break;
                }
                else {
                    EV << "successful inserted " << veh.str() << std::endl;
                    vi = i->second.erase(vi);
                    vehiclesCount[v.id] = vehiclesCount[v.id] + 1;
                }
            }
            else {

                // try to insert into desired lane
                EV << "trying to add " << veh.str() << " with " << route << " vehicle type " << type << std::endl;
                suc = commandInterface->addVehicle(veh.str(), type, route, simTime(), v.position, v.speed, v.lane);

                if (suc) {
                    EV << "successful inserted " << veh.str() << std::endl;
                    vi = i->second.erase(vi);
                    vehiclesCount[v.id] = vehiclesCount[v.id] + 1;
                }
                else {
                    if (!insertInOrder) {
                        vi++;
                    }
                    else {
                        break;
                    }
                }
            }
        }
    }
}

void TraCIBaseTrafficManager::addVehicleToQueue(int routeId, struct Vehicle v){
    vehicleInsertQueue[routeId].push_back(v);
    vehData.push_back(nodeData());
}

void sendData(int index, double speed, double acceleration, double positionX, double positionY, double CO2_emission, double fuel_consumption){
    int startPositionData[] = {110,55,5,130,40,0,120,70,10,100,50,15,95,35,8,75,50,3};
    vehData[index].speed = speed;
    vehData[index].acceleration = acceleration;
    vehData[index].positionX = positionX;
    vehData[index].positionY = positionY;
    vehData[index].id = index;
    vehData[index].fuel_in_timestep = fuel_consumption;
    vehData[index].co2_in_timestep = CO2_emission;
//
    vehData[index].CO2_emission = CO2_emission + vehData[index].CO2_emission;
    vehData[index].fuel_consumption = fuel_consumption + vehData[index].fuel_consumption;

//    if (simTime() <= 77){
//        vehData[index].CO2_emission = CO2_emission + vehData[index].CO2_emission;
//        vehData[index].fuel_consumption = fuel_consumption + vehData[index].fuel_consumption;
//
//    }
//    if (50 < simTime <= 65){
//        switch (index){
//        case 0:{
//            vehData[index].CO2_emission = CO2_emission*0.02 + vehData[index].CO2_emission;
//
//        }
//
//        }
//    }
//    if (65 < simTime <= 68){
//
//    }
//    if (68 < simTime <= 77){
//
//    }
//    if (simTime() > 77){
//        switch (index){
//        case 1:
//        case 13:
//        case 11:{
//            vehData[index].CO2_emission = CO2_emission*0.9733 + vehData[index].CO2_emission;
//            vehData[index].fuel_consumption = fuel_consumption*0.9733 + vehData[index].fuel_consumption;
//            break;
//        }
//        case 4:
//        case 17:
//        case 2: {
//            vehData[index].CO2_emission = CO2_emission*0.961 + vehData[index].CO2_emission;
//            vehData[index].fuel_consumption = fuel_consumption*0.961 + vehData[index].fuel_consumption;
//            break;
//        }
//
//        case 7:
//        case 5:
//        case 10:
//        case 14:
//        case 3:
//        case 6:{
//            vehData[index].CO2_emission = CO2_emission*0.9548 + vehData[index].CO2_emission;
//            vehData[index].fuel_consumption = fuel_consumption*0.9548 + vehData[index].fuel_consumption;
//            break;
//        }
//        default :{
//            vehData[index].CO2_emission = CO2_emission + vehData[index].CO2_emission;
//            vehData[index].fuel_consumption = fuel_consumption + vehData[index].fuel_consumption;
//            break;
//        }
//        }
//
//    }
    if ((positionX - startPositionData[index]) >= 50){

            EV << "Vehicle data: " << "Node " << index << ", speed " << vehData[index].speed << ", posX " << vehData[index].positionX << ", posY " << vehData[index].positionY << ", CO2_emission " << vehData[index].CO2_emission << ", fuel_consumption" << vehData[index].fuel_consumption << ", CO2_eimssion_in_timestep" << vehData[index].co2_in_timestep << ", fuel_consumption_in_timestep" << vehData[index].fuel_in_timestep << endl;
            }
            else {
                EV << "not reached till 100 mark " << simTime() << endl;
            }
   // EV << "Vehicle data: " << "Node " << index << ", speed " << vehData[index].speed << ", posX " << vehData[index].positionX << ", posY " << vehData[index].positionY << " , CO2 emission " << vehData[index].CO2_emission << " , fuel_consumption" << vehData[index].fuel_consumption <<  ", CO2_eimssion_in_timestep" << vehData[index].co2_in_timestep << endl;
}
//void sendData(int index, double speed, double acceleration, double positionX, double positionY, double CO2_emission, double fuel_consumption){
//
//    int startPositionData[] = {5,10};
//    vehData[index].speed = speed;
//    vehData[index].acceleration = acceleration;
//    vehData[index].positionX = positionX;
//    vehData[index].positionY = positionY;
//    vehData[index].CO2_emission = CO2_emission + vehData[index].CO2_emission;
//    vehData[index].fuel_consumption = fuel_consumption + vehData[index].fuel_consumption;
//    vehData[index].co2_in_timestep = CO2_emission;
//    if ((positionX - startPositionData[index])== 1000){
//    EV << "Vehicle data: " << "Node " << index << ", speed " << vehData[index].speed << ", posX " << vehData[index].positionX << ", posY " << vehData[index].positionY << ", CO2_emission " << vehData[index].CO2_emission << ", fuel_consumption" << vehData[index].fuel_consumption << ", CO2_eimssion_in_timestep" << vehData[index].co2_in_timestep << endl;
//    }
//    else {
//        EV << "not reached till 1000 mark " << endl;
//    }
//    }

std::vector<nodeData> getData() {
    return vehData;
}
