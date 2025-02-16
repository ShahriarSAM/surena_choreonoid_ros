#include <cnoid/SimpleController>
#include <cnoid/Sensor>
#include <cnoid/BodyLoader>
#include <eigen3/Eigen/Eigen>
#include <ros/ros.h>
#include "trajectory_planner/Trajectory.h"
#include "trajectory_planner/JntAngs.h"
#include "trajectory_planner/GeneralTraj.h"
#include "std_srvs/Empty.h"

using namespace std;
using namespace cnoid;
using namespace Eigen;
//SR1
/*
const double pgain[] = {
    8000.0, 8000.0, 8000.0, 8000.0, 8000.0, 8000.0,
    3000.0, 3000.0, 3000.0, 3000.0, 3000.0, 3000.0, 3000.0,
    8000.0, 8000.0, 8000.0, 8000.0, 8000.0, 8000.0,
    3000.0, 3000.0, 3000.0, 3000.0, 3000.0, 3000.0, 3000.0,
    8000.0, 8000.0, 8000.0 };
*/
//Surena IV
const double pgain[] = {
    10000.0, 10000.0, 10000.0, 10000.0, 10000.0, 10000.0,
    10000.0, 10000.0, 10000.0, 10000.0, 10000.0, 10000.0, 8000.0,
    8000.0, 3000.0, 3000.0, 3000.0, 3000.0, 3000.0,
    3000.0, 3000.0, 3000.0, 3000.0, 3000.0, 3000.0, 3000.0,
    3000.0, 3000.0, 3000.0 };

const double dgain[] = {
    100.0, 100.0, 100.0, 100.0, 100.0, 100.0,
    100.0, 100.0, 100.0, 100.0, 100.0, 100.0, 100.0,
    100.0, 100.0, 100.0, 100.0, 100.0, 100.0,
    100.0, 100.0, 100.0, 100.0, 100.0, 100.0, 100.0,
    100.0, 100.0, 100.0 };


class SurenaController : public SimpleController{
  
    bool result;
    ros::NodeHandle nh;
    int idx = 0;
    double dt;
    double qref[29];
    double qold[29];
    BodyPtr ioBody;
    ForceSensor* leftForceSensor;
    ForceSensor* rightForceSensor;
    AccelerationSensor* accelSensor;
    RateGyroSensor* gyro;
    int surenaIndex_[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    int sr1Index_[12] = {2, 0, 1, 3, 4, 5, 15, 13, 14, 16, 17, 18};

    int size_;

public:

    virtual bool initialize(SimpleControllerIO* io) override
    {
        dt = io->timeStep();
        // General Motion
        ros::ServiceClient gen_client=nh.serviceClient<trajectory_planner::GeneralTraj>("/general_traj");
        trajectory_planner::GeneralTraj general_traj;
        general_traj.request.init_com_pos = {0, 0, 0.73};
        general_traj.request.init_com_orient = {0, 0, 0};
        general_traj.request.final_com_pos = {0, 0, 0.68};
        general_traj.request.final_com_orient = {0, 0, 0};

        general_traj.request.init_lankle_pos = {0, 0.115, 0};
        general_traj.request.init_lankle_orient = {0, 0, 0};
        general_traj.request.final_lankle_pos = {0, 0.115, 0};
        general_traj.request.final_lankle_orient = {0, 0, 0};

        general_traj.request.init_rankle_pos = {0, -0.115, 0};
        general_traj.request.init_rankle_orient = {0, 0, 0};
        general_traj.request.final_rankle_pos = {0, -0.115, 0};
        general_traj.request.final_rankle_orient = {0, 0, 0};

        general_traj.request.time = 2;
        general_traj.request.dt = dt;

        //DCM Walk
        ros::ServiceClient client=nh.serviceClient<trajectory_planner::Trajectory>("/traj_gen");
        trajectory_planner::Trajectory traj;

        traj.request.step_width = 0.0;
        traj.request.alpha = 0.44;
        traj.request.t_double_support = 0.15;
        traj.request.t_step = 1;
        traj.request.step_length = -0.1;
        traj.request.COM_height = 0.68;
        traj.request.step_count = 4;
        traj.request.ankle_height = 0.025;
        traj.request.theta = 0.0;
        traj.request.dt = dt;
        result = traj.response.result;
        
        size_ = int(((traj.request.step_count + 2) * traj.request.t_step + general_traj.request.time) / traj.request.dt);

        result = true;

        gen_client.call(general_traj);
        client.call(traj);
        
        ioBody = io->body();
        leftForceSensor = ioBody->findDevice<ForceSensor>("LeftAnkleForceSensor");
        rightForceSensor = ioBody->findDevice<ForceSensor>("RightAnkleForceSensor");
        io->enableInput(leftForceSensor);
        io->enableInput(rightForceSensor);
        accelSensor = ioBody->findDevice<AccelerationSensor>("WaistAccelSensor");
        io->enableInput(accelSensor);
        //gyro = ioBody->findDevice<RateGyroSensor>("WaistGyro"); //SR1
        gyro = ioBody->findDevice<RateGyroSensor>("gyrometer"); //SurenaIV
        io->enableInput(gyro);

        for(int i=0; i < ioBody->numJoints(); ++i){
            Link* joint = ioBody->joint(i);
            joint->setActuationMode(Link::JOINT_TORQUE);
            io->enableIO(joint);
            qref[i] = joint->q();
            qold[i] = qref[i];
            
        }
        return true;
    }
    virtual bool control() override
    {
        ros::ServiceClient jnt_client = nh.serviceClient<trajectory_planner::JntAngs>("/jnt_angs");
        trajectory_planner::JntAngs jntangs;
        if (idx < size_ - 1){
            jntangs.request.left_ft = {float(leftForceSensor->f().z()),
                                    float(leftForceSensor->tau().x()),
                                    float(leftForceSensor->tau().y())};
            jntangs.request.right_ft = {float(rightForceSensor->f().z()),
                                    float(rightForceSensor->tau().x()),
                                    float(rightForceSensor->tau().y())};
                                    
            jntangs.request.iter = idx;
            double cur_q[ioBody->numJoints()];
            for(int i=0; i < ioBody->numJoints(); ++i){
                    Link* joint = ioBody->joint(i);
                    cur_q[i] = joint->q();
            }

            for (int j=0; j<12; j++){
                jntangs.request.config[j] = cur_q[surenaIndex_[j]];
                jntangs.request.jnt_vel[j] = (cur_q[surenaIndex_[j]] - qold[surenaIndex_[j]]) / dt;
                }
            jntangs.request.accelerometer = {accelSensor->dv()(0),accelSensor->dv()(1),accelSensor->dv()(2)};
            jntangs.request.gyro = {float(gyro->w()(0)),float(gyro->w()(1)),float(gyro->w()(2))};

            if (result){

                jnt_client.call(jntangs);

                for (int j=0; j<12; j++)
                    qref[surenaIndex_[j]] = jntangs.response.jnt_angs[j];
                    
                for(int i=0; i < ioBody->numJoints(); ++i){
                    Link* joint = ioBody->joint(i);
                    double q = joint->q();
                    double dq = (q - qold[i]) / dt;
                    double u = (qref[i] - q) * pgain[i] + (0.0 - dq) * dgain[i];
                    qold[i] = q;
                    joint->u() = u;
                }
            }
            idx ++;
        }else{
            if(idx == size_ - 1){
                ros::ServiceClient reset_client = nh.serviceClient<std_srvs::Empty>("/reset_traj");
                std_srvs::Empty srv;
                reset_client.call(srv);
            }
            for(int i=0; i < ioBody->numJoints(); ++i){
                Link* joint = ioBody->joint(i);
                double q = joint->q();
                double dq = (q - qold[i]) / dt;
                double u = (qref[i] - q) * pgain[i] + (0.0 - dq) * dgain[i];
                qold[i] = q;
                joint->u() = u;
            }
            idx ++;
        }
        return true;
    }
};
CNOID_IMPLEMENT_SIMPLE_CONTROLLER_FACTORY(SurenaController)