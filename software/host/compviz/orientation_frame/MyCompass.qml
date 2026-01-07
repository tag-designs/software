import QtQuick
//import QtQuick.Controls.macOS
import QtQuick.Controls
import QtQuick.Layouts



Rectangle {

    id: root
    border.color: "black"
    border.width: 1
    color: "#F5F5F5"

    Layout.alignment: Qt.AlignHCenter
    //Layout.preferredWidth: 300
    //Layout.preferredHeight: 400

    property alias heading: headingText.text
    property alias dip: dipText.text
    property alias roll: rollText.text
    property alias pitch: pitchText.text
    property alias field: fieldText.text
    property alias rotation: dialface.rotation

    property bool batteryForward: true
    property double declination: 0.0

    function setOrientation(h,p,r,d,f) {
        if (!batteryForward) {
            h = (h + 180)%360
            p = -p
            r = -r
        }
        rotation = -h
        heading = Number(h).toLocaleString(Qt.locale(), 'f', 0)
        pitch = Number(p).toLocaleString(Qt.locale(), 'f', 0)
        roll = Number(r).toLocaleString(Qt.locale(), 'f', 0)
        dip = Number(d).toLocaleString(Qt.locale(), 'f', 0)
        field = Number(f).toLocaleString(Qt.locale(), 'f', 0)
        //console.info("setoreintation called");
    }

    function setBatteryForward(f){
        batteryFoward = f
    }

    function setDeclination(d) {
        declination = d
    }

    //color: "#fffae7"

    ColumnLayout {
        spacing: 5
        Layout.alignment: Qt.AlignHCenter
        anchors.horizontalCenter: parent.horizontalCenter
        Layout.preferredWidth: 275

        // spacer

        Item {
            Layout.preferredHeight: 5
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
                width: 225
                height: 225
            }
            Image {
                id: dialCase
                source: "qrc:/qfi/orientation_frame/hi_case.svg"
                width: 225
                height: 225
            }
        }

         Item {
            Layout.preferredHeight: 5
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
            Layout.preferredHeight: 5
        }
        /*
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
        }*/
        Item {
            //This acts as a flexible horizontal spacer
            Layout.fillHeight: true
        }
    }
}
