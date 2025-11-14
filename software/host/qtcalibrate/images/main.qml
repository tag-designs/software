import QtQuick
import QtQuick.Controls
import QtQuick3D
import QtQuick3D.AssetUtils
import QtQuick3D.Helpers
import QtQuick.Scene3D

View3D {

    id: view3d
    anchors.fill: parent   

    function setRotationQuaternion(qt){
        myGroup.rotation = qt 
    }

    function setScreenDirection(direction){
        originNode.eulerRotation = Qt.vector3d(0.0,0.0,direction)
    }

    function setBatteryForward(isBatteryForward){
        if (isBatteryForward) {
            myGroup.eulerRotation = Qt.vector3d(0.0,0.0,0.0)
        } else {
        }
    }
    
    
    Node {
        id: viewNode
           
        Node {
            id: originNode
            position: Qt.vector3d(0,0,0)
         
            
            PerspectiveCamera {
                id: camera
                position: Qt.vector3d(0, 500, 40)
                pivot: Qt.vector3d(0,0,0)

                eulerRotation.x: -90
                eulerRotation.y: -180
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
        
        }
        
    Node {
            id: myGroup

            Model {
                source: "#Cube"
                materials: [ PrincipledMaterial { baseColor: "green" } ]
                position: Qt.vector3d(0, 0, 0)
                scale: Qt.vector3d(1,2,0.1)
            }

            Model {
                source: "#Cylinder"
                materials: [ PrincipledMaterial { baseColor: "silver" } ]
                position: Qt.vector3d(0, -100, 20)
                scale: Qt.vector3d(1.1,0.3,1.1)
                eulerRotation: Qt.vector3d(90, 0, 0)
            }
        }
   
      AxisHelper {
                enableXZGrid: true
            }

    }
   WasdController {
      controlledObject : camera
  }
 
}

   


