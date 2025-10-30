import QtQuick
import QtQuick.Controls
import QtQuick3D
import QtQuick3D.AssetUtils
import QtQuick3D.Helpers

View3D {

    function setRotation(pitch, roll){
        myGroup.eulerRotation.z = -pitch
        myGroup.eulerRotation.x = roll+180
        //return "ok"
    }
    id: view3d
    anchors.fill: parent

    // Set up a camera for viewing the scene.
    OrthographicCamera {
        id: camera
        position: Qt.vector3d(0,0, 500)
        //eulerRotation.x: -30
        lookAtNode: myGroup
    }

    // Add a directional light.
    
    DirectionalLight {
        eulerRotation.x: -30
    }

    Node {
        id: myGroup
        position: Qt.vector3d(0, 0, 0)
        eulerRotation.y: 90 // Rotate the entire group

        Model {
            source: "#Cube"
            materials: [ PrincipledMaterial { baseColor: "green" } ]
            //position: Qt.vector3d(-100, 0, 0)
            scale: Qt.vector3d(2, .05, 1)
        }

        Model {
            source: "#Cylinder"
            materials: [ PrincipledMaterial { baseColor: "silver" } ]
            position: Qt.vector3d(100, 18, 0)
            scale: Qt.vector3d(1.1,0.3,1.1)
        }
    }
}


