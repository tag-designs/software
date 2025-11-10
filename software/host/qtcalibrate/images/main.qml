import QtQuick
import QtQuick.Controls
import QtQuick3D
import QtQuick3D.AssetUtils
import QtQuick3D.Helpers

View3D {

    function setRotation(yaw, pitch, roll){
       // myGroup.eulerRotation.y = -yaw-90    // green
        myGroup.eulerRotation.y = -pitch    // red
        myGroup.eulerRotation.x = -roll // blue
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
            id: originNode
            x: 0
            y: 0
            z: 0
            PerspectiveCamera {
                id: cameraNode
                y: -500
                z: 100
                lookAtNode: myGroup
            }
        }

        OrbitCameraController {
            anchors.fill: parent
            origin: originNode
            camera: cameraNode
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
    }
   

        
       Node {
            id: myGroup
            position: Qt.vector3d(0, 0, 0)
            //eulerRotation.y: 90 // Rotate the entire group

            Model {
                source: "#Cube"
                materials: [ PrincipledMaterial { baseColor: "green" } ]
                position: Qt.vector3d(0, 0, 0)
                scale: Qt.vector3d(1, 2, .1)
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

   


