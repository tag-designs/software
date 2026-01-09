#include <cmath>
#include <cfloat>

#include <QList>
#include <QVector3D>
#include <QQuaternion>
#include <QMatrix3x3>
#include <QObject>

#include "mainwindow.h"


//#include <iostream>


void MainWindow::apply_calibration(QVector3D &mag){
	float x, y, z;

	x = mag[0] - Vcal[0];
	y = mag[1] - Vcal[1];
	z = mag[2] - Vcal[2];
	mag[0] = (x * Acal[0][0] + y * Acal[0][1] + z * Acal[0][2]);
	mag[1] = (x * Acal[1][0] + y * Acal[1][1] + z * Acal[1][2]);
	mag[2] = (x * Acal[2][0] + y * Acal[2][1] + z * Acal[2][2]);
}


/*
Keeping a Good Attitude: A Quaternion-Based Orientation Filter for IMUs and MARGs
Authors
Roberto G. Valenti, CUNY City College
Ivan Dryanovski, CUNY Graduate Center
Jizhong Xiao, CUNY City College

Algorithm assumes north west up global frame 
*/

#undef DEBUG
//#define DEBUG 1


bool MainWindow::eCompass(QVector3D magin, QVector3D accel, QQuaternion &q, 
                            float& dip, float& field, float& mg)
{
	
	QQuaternion qacc;
	//float alpha = 0.4; // filter coefficient
	float q0,q1,q2,q3;

	apply_calibration(magin);

	// algorithm uses positive g as up, accelerometer measures this as negative g, swapping all three
	// changes handedness of vector

	if ((magin.length() == 0.0) || (accel.length() == 0.0))
		return false;
	
	field = magin.length();
	mg = accel.length();
	accel.normalize(); 
	magin.normalize();

	// switch to NWU convention with gravity positive (accel measures opposite sign)

	accel = QVector3D(accel[0],-accel[1],accel[2]);
	magin = QVector3D(magin[0],magin[1],-magin[2]);
	
	// qacc is the quaternion obtained from the acceleration vector
    // representing the orientation of the Global frame wrt the Local frame with
    // arbitrary yaw (intermediary frame).

	
	
    if (accel.z() >= 0)
    {
        q0 =  sqrt((accel.z() + 1) * 0.5);;
        q1 = -accel.y() / (2.0 * q0);
        q2 = accel.x() / (2.0 * q0);
        q3 = 0;
    } else
    {
        double X = sqrt((1 - accel.z()) * 0.5);
        q0 = -accel.y() / (2.0 * X);
        q1 = X;
        q2 = 0;
        q3 = accel.x() / (2.0 * X);
    }

	// need to correct quaternion for  directional difference in gravity
	//qacc = QQuaternion(0,1,0,0)*QQuaternion(q0,q1,q2,q3)*QQuaternion(0,0,1,0);
	//qacc = QQuaternion(q3,-q2,-q1,q0);
	qacc = QQuaternion(q0,q2,q1,q3);
	qacc.normalize();
    QVector3D angles;
#ifdef DEBUG_
	// Tests
	angles = qacc.toEulerAngles();
	fprintf(stderr, "\rPitch: %3.2f Roll: %3.2f", angles[1], angles[0]);
	std::cerr <<  "\rPitch: " << angles[1] << " Roll: " << angles[0];
	return false;

	
    QVector3D atest1 =  qacc.conjugated().rotatedVector(accel);
	QVector3D atest2 =  qacc.rotatedVector(QVector3D(0,0,1));
	qInfo() << "A In:  " << accel;
	qInfo() << "A test1" << atest1 << " == " << QVector3D(0,0,1);
	qInfo() << "A test2" << accel << " == " << atest2;
	qInfo() << "A Out: " << qacc.rotatedVector(accel);
	
#endif
//
	// q_mag is the quaternion that rotates the Global frame (North West Up)
    // into the intermediary frame. q1_mag and q2_mag are defined as 0.

	// note this step!  Apply conjugated quarternion for calculating
	// qmag

   
	//QVector3D mag = qacc.conjugated().rotatedVector(magin);
	QVector3D mag = qacc.rotatedVector(magin);


	float gamma = mag[0]*mag[0] + mag[1]*mag[1];
	float beta  = sqrt(gamma + mag[0] * sqrt(gamma));
	float betaneg = sqrt(gamma - mag[0]* sqrt(gamma));
	float q0_mag, q3_mag;

	if (mag[0] >= 0.0) {
		q0_mag =  beta/sqrt(2.0*gamma);
		q3_mag = mag[1]/(sqrt(2.0) * beta);
	} else {
		q0_mag = mag[1]/(sqrt(2.0) * betaneg);
		q3_mag = betaneg/sqrt(2.0*gamma);
	}

	// tweak rotation direction and heading

	QQuaternion qmag(q0_mag,0,0,-q3_mag);
	qmag = QQuaternion(sqrt(2)/2,0,0,sqrt(2)/2) * qmag;
	qmag.normalize();

	q = qacc*qmag;
	q.normalize();
	q.setX(-q.x());
	q.setY(-q.y());

	dip = atan2(mag[2],sqrt(gamma))*180.0/M_PI;
#ifdef DEBUG
	angles = q.toEulerAngles();
	if (angles[2] < 0.0) angles[2] += 360.0;
	fprintf(stderr, "\rPitch: %3.2f Roll: %3.2f Yaw: %3.2f Dip: %3.2f", 
			angles[0], angles[1],angles[2],dip);
	return false;
	//qInfo() << "Pitch:" << angles[1] << " Roll: " << angles[0] << " Yaw:" << angles[2];
	QVector3D magtest(sqrt(gamma), 0, mag[2]);
	qInfo() << "Mag test: " << qmag.rotatedVector(mag) << " == " << magtest;
#endif

#ifdef DEBUG
	QVector3D mtmp = qacc.rotatedVector(magin);
	float yaw = atan2(mtmp[1],mtmp[0])*180.0/M_PI;
	float yaw2 = asin(q3_mag)*360.0/M_PI;
	// run some tests
	//qInfo() << "A test 3" << accel << " == " << q.rotatedVector(QVector3D(0,0,1));
	//qInfo() << "A final: " << q.rotatedVector(accel);
	//qInfo() << "M In:  " << magin;
	//qInfo() << "M acc:" << qacc.rotatedVector(magin);
	//qInfo() << "M Out: " << q.rotatedVector(magin);
#endif
#ifdef DEBUG
	qInfo() << "yaw " << yaw << " yaw2 " << yaw2;
	qInfo() << "qac: " << qacc.vector();
	qInfo() << "qmag " << qmag.vector();
	//qInfo() << "q: " << q.vector();
	
#endif
	return true;
}

