import QtQuick
import QtQuick.Controls
import QtQuick3D
import QtQuick3D.AssetUtils
import QtQuick3D.Helpers
import QtQuick.Scene3D

//Window {
//  id: root
//  width: 640
//  height: 960
//  visible: true
//  title: qsTr("QML 3D")

View3D {

    id: view3d
    anchors.fill: parent   

    function setRotationQuaternion(qt){
        if (myGroup.batteryForward) {
            myGroup.rotation = qt;
        } else {
            myGroup.rotation = qt.inverted();
        }
        console.info("setRotationQuaternion Called");
    }

    function setScreenDirection(direction){
        goNode.eulerRotation = Qt.vector3d(0.0,direction,0.0);
        console.info("setScreenDirection called");
    }

    function setBatteryForward(isBatteryForward){

        myGroup.batteryForward = isBatteryForward;
       
        if (isBatteryForward) {
            myGroup.batteryPosition = 100;
            console.info("setting battery forward");
        } else {
            myGroup.batteryPosition = -100;
            console.info("clearing battery forward");
        }
    } 
   
    
    Node {
        id: viewNode    
        eulerRotation.x: -90
        

        Node {
            id: originNode
            position: Qt.vector3d(0,0,0)
                eulerRotation.x: 90
                eulerRotation.y: 90
              

            Node {
                id: orientationNode
                PerspectiveCamera {
                    id: camera
                    position: Qt.vector3d(0, 40, -500)
                    lookAtNode: orientationNode
                }
                
                OrbitCameraController {
                    anchors.fill: parent
                    origin: originNode
                    camera: camera
                    mouseEnabled: true
                    panEnabled: true
                }

                DirectionalLight {
                  eulerRotation.x: 90
                  scale: Qt.vector3d(4,4,-4)
                }

                DirectionalLight {
                    eulerRotation.x: -90
                    scale: Qt.vector3d(4,4,4)
                }

                 DirectionalLight {
                    eulerRotation.y: -90
                    scale: Qt.vector3d(4,4,4)
                }

                DirectionalLight {
                    eulerRotation.y: 90
                    scale: Qt.vector3d(4,4,4)
                }
            }

            Node {
            id: goNode
            //eulerRotation: orientationNode.eulerRotation

                Node {
                    id: myGroup
                    property bool batteryForward: true
                    property int batteryPosition: 100
                    //position: Qt.vector3d(0, 0, 0) 
                    Model {
                        //eulerRotation: Qt.vector3d(0,0,180)
                        source: "#Cube"
                        materials: [ PrincipledMaterial { baseColor: "green" } ]
                        position: Qt.vector3d(0, 0, 0)
                        scale: Qt.vector3d(1,0.1,2)
                    }

                    Model {
                        source: "#Cylinder"
                        materials: [ PrincipledMaterial { baseColor: "silver" } ]
                        position: Qt.vector3d(0,20,myGroup.batteryPosition)
                        scale: Qt.vector3d(1.1,0.3,1.1)
                        //eulerRotation: Qt.vector3d(90, 0, 0)
                        castsShadows: true
                        castsReflections: true
                    }
            
                }         
                AxisHelper {
                    enableXYGrid: true
                }
            
            }
       
        
    }

    WasdController {
        controlledObject : camera
    }
 
    }
}
//}

   


