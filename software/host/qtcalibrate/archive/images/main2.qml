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
        myGroup.rotation = qt 
    }

    function setScreenDirection(direction){
        orientationNode.eulerRotation = Qt.vector3d(0.0,0.0,direction)
        //originNode.rotation = Qt.Quaternion.fromAxisAndAngle(Qt.vector3d(0, 0, 1), direction)
    }

    function setBatteryForward(isBatteryForward){
        if (isBatteryForward) {
            myGroup.eulerRotation = Qt.vector3d(0.0,0.0,0.0)
        } else {
        }
    }   
    
    Node {
        id: viewNode    
        //eulerRotation.x: -90
        

        Node {
            id: originNode
            position: Qt.vector3d(0,0,0)
            eulerRotation.y:-90
            eulerRotation.x:-90
           AxisHelper {
                enableXYGrid: true
                enableXZGrid: false
                }
           

            Node {
                id: orientationNode
                //eulerRotation.y: 180
               
                PerspectiveCamera {
                    id: camera
                    position: Qt.vector3d(0, -20, -500)
                    //pivot: Qt.vector3d(0,0,0)
                    //eulerRotation.z: 180
                    //eulerRotation.x: 90 

                    lookAtNode: originNode
                }
                
                OrbitCameraController {
                    anchors.fill: parent
                    origin: originNode
                    camera: camera
                    mouseEnabled: true
                    panEnabled: true
                }

                DirectionalLight {
                    eulerRotation.y: 30
                }

                DirectionalLight {
                    eulerRotation.y: -30
                }
            }

            Node {
            id: goNode
            //eulerRotation: orientationNode.eulerRotation

                Node {
                    id: myGroup
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
                        position: Qt.vector3d(20,0, 100)
                        scale: Qt.vector3d(1.1,0.3,1.1)
                        //eulerRotation: Qt.vector3d(90, 0, 0)
                        castsShadows: true
                        castsReflections: true
                    }
                   
            }
            
        }  
            
        }
       
        
    }

    WasdController {
        controlledObject : camera
    }
 
}
//}

   


