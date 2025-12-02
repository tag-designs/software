import QtQuick
import QtQuick.Controls.macOS
import QtQuick.Controls
import QtQuick3D
import QtQuick3D.AssetUtils
import QtQuick3D.Helpers
import QtQuick.Scene3D
import QtQuick.Layouts

Rectangle {
    id: root
    Layout.alignment: Qt.AlignHCenter
    Layout.preferredWidth: 300
    Layout.preferredHeight: 550
    color: "#F5F5F5"
    border.color: "black"
    border.width: 1

    property alias batteryForward: myGroup.batteryForward
    property alias rotation: myGroup.rotation

    ColumnLayout {
        spacing: 5
        Layout.alignment: Qt.AlignHCenter
        anchors.horizontalCenter: parent.horizontalCenter
        Layout.preferredWidth: 275

        // spacer

        Item {
            Layout.preferredHeight: 10
        }

        Rectangle {
            id: viewRectangle
            border.color: "black"
            //color: "transparent"
            //color: "#fffae3"
            Layout.fillWidth: true
            Layout.preferredWidth: 250
            Layout.preferredHeight: 250
            Layout.alignment: Qt.AlignHCenter

            View3D {

                id: view3d
                width: 250
                height: 250

                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                anchors.centerIn: parent

                environment: SceneEnvironment {
                    antialiasingMode: SceneEnvironment.MSAA
                }

                Node {
                    id: originNode
                    position: Qt.vector3d(0, 0, 0)
                    //eulerRotation.y: -90

                    Node {
                        id: orientationNode
                        position: Qt.vector3d(0, 0, 0)
                        PerspectiveCamera {
                            id: camera
                            position: Qt.vector3d(0, -500, 40)
                            lookAtNode: goNode
                            fieldOfView: 70
                        }

                        OrbitCameraController {
                            anchors.fill: parent
                            origin: originNode
                            camera: camera
                            mouseEnabled: true
                            panEnabled: true
                        }

                        DirectionalLight {
                            eulerRotation.x: 45
                            scale: Qt.vector3d(4, 4, -4)
                        }

                        DirectionalLight {
                            eulerRotation.x: -45
                            scale: Qt.vector3d(4, 4, 4)
                        }

                        DirectionalLight {
                            eulerRotation.y: -45
                            scale: Qt.vector3d(4, 4, 4)
                        }

                        DirectionalLight {
                            eulerRotation.y: 45
                            scale: Qt.vector3d(4, 4, 4)
                        }
                    }

                    Node {
                        id: goNode
                        position: Qt.vector3d(0, 0, 0)
                        Node {
                            id: myGroup
                            property bool batteryForward: true
                            property int batteryPosition: 100 //batteryForward ? 100 : -100
                            Model {
                                source: "#Cube"
                                position: Qt.vector3d(0, 0, 0)
                                scale: Qt.vector3d(1, 2, 0.1)
                                materials: [
                                    PrincipledMaterial {
                                        baseColor: "green"
                                    }
                                ]
                            }

                            Model {
                                source: "#Cylinder"
                                position: Qt.vector3d(0, myGroup.batteryPosition, 20)
                                eulerRotation.x: 90
                                scale: Qt.vector3d(1.1, 0.3,1.1)
                                materials: [
                                    PrincipledMaterial {
                                        baseColor: "silver"
                                    }
                                ]
                                castsShadows: true
                                castsReflections: true
                            }
                        }
                        AxisHelper {
                            enableXYGrid: true
                            enableXZGrid: false
                        }
                    }
                }

                WasdController {
                    controlledObject: camera
                }
            }
        }

        Item {
            Layout.preferredHeight: 20
        }
        Rectangle {
             //Layout.alignment: Qt.AlignHCenter

        

                    RowLayout {

                        Text {
                            //Layout.alignment: Qt.AlignHCenter
                            text: "Screen Direction:"
                        }
                        SpinBox {
                            //Layout.alignment: Qt.AlignHCenter
                            from: 0
                            to: 360
                            stepSize: 5
                            editable: true
                            onValueChanged: goNode.eulerRotation = Qt.vector3d(
                                                0.0, 0.0, value)
                        }
                    }
    
            //    Item {
            //      // This acts as a flexible vertical spacer
            //      Layout.fillHeight: true
            // }
        }
    }
}
