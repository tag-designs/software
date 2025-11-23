import QtQuick
import QtQuick.Controls.macOS
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {

    id: root
    border.color: "black"
    border.width: 1
    color: "#F5F5F5"

    Layout.alignment: Qt.AlignHCenter
    Layout.preferredWidth: 300
    Layout.preferredHeight: 500
    //color: "#fffae7"

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
            id: headingIndicator
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 225
            Layout.preferredHeight: 225
            color: "transparent"

            Image {
                id: dialface
                source: "qrc:/hi_face.svg"
                rotation: -45
            }
            Image {
                id: dialCase
                source: "qrc:/hi_case.svg"
            }
        }
        GroupBox {

            Layout.alignment: Qt.AlignHCenter
            //border.color: "black"
            //border.width: 1
            title: "Sensors"

            GridLayout {

                id: grid
                //anchors.fill: parent
                columns: 2 // Defines a grid with 2 columns
                rowSpacing: 5
                columnSpacing: 5
                Layout.alignment: Qt.AlignHCenter

                Text {
                    text: "Heading:"
                }

                TextField {
                    id: headingText
                    text: "0.0"
                    horizontalAlignment: TextInput.AlignRight
                    readOnly: true
                }

                Text {
                    text: "Roll:"
                }

                TextField {
                    id: rollText
                    text: "0.0"
                    horizontalAlignment: TextInput.AlignRight
                    readOnly: true
                }

                Text {
                    text: "Pitch:"
                }

                TextField {
                    id: pitchText
                    text: "0.0"
                    horizontalAlignment: TextInput.AlignRight
                    readOnly: true
                }
                Text {
                    text: "Dip Angle:"
                }

                TextField {
                    id: dipText
                    text: "0.0"
                    horizontalAlignment: TextInput.AlignRight
                    readOnly: true
                }

                Text {
                    text: "Field:"
                }

                TextField {
                    id: fieldText
                    text: "0.0"
                    horizontalAlignment: TextInput.AlignRight
                    readOnly: true
                }
            }
        }
        Item {
            //This acts as a flexible horizontal spacer
            Layout.fillHeight: true
        }
    }
}
