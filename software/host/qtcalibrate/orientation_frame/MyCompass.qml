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
    Layout.preferredHeight: 550

    property alias heading: headingText.text
    property alias dip: dipText.text
    property alias roll: rollText.text
    property alias pitch: pitchText.text
    property alias field: fieldText.text
    property alias rotation: dialface.rotation

    property bool batteryForward: true

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
                source: "qrc:/qfi/orientation_frame/hi_face.svg"
                rotation: -45
            }
            Image {
                id: dialCase
                source: "qrc:/qfi/orientation_frame/hi_case.svg"
            }
        }

         Item {
            Layout.preferredHeight: 10
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
            Layout.preferredHeight: 10
        }
        GroupBox {

                    Layout.alignment: Qt.AlignHCenter
                    //border.color: "black"
                    //border.width: 1
                    title: "Tag Position"
                RowLayout {
                                Text {
                                    text: "Battery Direction:"
                                    Layout.alignment: Qt.AlignTop
                                }
                                ColumnLayout {
                                    RadioButton {
                                        checked: true
                                        text: qsTr("Forward")
                                        onCheckedChanged: {
                                            if (checked) {
                                                root.batteryForward = true
                                            }
                                        }
                                    }
                                    RadioButton {
                                        text: qsTr("Backward")
                                        onCheckedChanged: {
                                            if (checked) {
                                                root.batteryForward = false
                                            }
                                        }
                                    }
                                }
                            }
        }
        Item {
            //This acts as a flexible horizontal spacer
            Layout.fillHeight: true
        }
    }
}
