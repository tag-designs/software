import QtQuick
import QtQuick.Controls
import QtQuick3D
import QtQuick3D.AssetUtils
import QtQuick3D.Helpers

View3D {

    function setRotation(yaw, pitch, roll){ 
        myGroup.eulerRotation.x = -pitch  
        myGroup.eulerRotation.y = -roll  
        //myGroup.eulerRotation: Qt.vector3d(yaw,pitch,roll)
        //return "ok"
    }

    function setRotationQuaternion(qt){
       // myGroup.eulerRotation.y = -yaw-90    // green
        myGroup.rotation = qt    // red
        //return "ok"
    }

    id: view3d
    anchors.fill: parent
   
 
    Node {
        id: viewNode

           
         Node {
            id: originNode
            x: 0
            y: 0
            z: 0
            
          
            PerspectiveCamera {
                id: camera
                position: Qt.vector3d(0, 500, 20)
                eulerRotation.z: 180
                lookAtNode: myGroup  
            }
         
            OrbitCameraController {
                anchors.fill: parent
                origin: originNode
                camera: camera
                mouseEnabled: true
                panEnabled: true
            }

            DirectionalLight {
                eulerRotation.x: 30
            }

            DirectionalLight {
                eulerRotation.x: -30
            }

                   AxisHelper {
                    enableXZGrid: false
            }
         }
        
        Node {
                id: myGroup
                position: Qt.vector3d(0, 0, 0)

                    Model {
                        source: "#Cube"
                        materials: [ PrincipledMaterial { baseColor: "green" } ]
                        position: Qt.vector3d(0, 0, 0)
                        scale: Qt.vector3d(1,2,0.1)
                    }

                    Model {
                        source: "#Cylinder"
                        materials: [ PrincipledMaterial { baseColor: "silver" } ]
                        position: Qt.vector3d(0, 100, 20)
                        scale: Qt.vector3d(1.1,0.3,1.1)
                        eulerRotation: Qt.vector3d(90, 0, 0)
                    }
                 

            }
   
    }
    WasdController {
        controlledObject : camera
    }
}

   


