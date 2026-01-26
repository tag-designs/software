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
    property alias gravity: gravityText.text
    property alias rotation: dialface.rotation

    property bool batteryForward: true
    property double declination: 0.0
    property double heading_value : 0.0

    function setHeading() {
        var h = heading_value + declination
        if (!batteryForward){
            h = h + 180
        }
        h = h%360
        rotation = -h
        heading = Number(h).toLocaleString(Qt.locale(), 'f', 0)
    }

    function setOrientation(h,p,r,d,f,g) {
        if (!batteryForward) {
            p = -p
            r = -r
        }
        heading_value = h
        setHeading()
        
        pitch = Number(p).toLocaleString(Qt.locale(), 'f', 0)
        roll = Number(r).toLocaleString(Qt.locale(), 'f', 0)
        dip = Number(d).toLocaleString(Qt.locale(), 'f', 0)
        field = Number(f).toLocaleString(Qt.locale(), 'f', 0)
        gravity = Number(g).toLocaleString(Qt.locale(), 'f', 0)
        //console.info("setoreintation called");
    }

    function setBatteryForward(f){
        //console.info(f)
        batteryForward = f
        if (f){
            batteryText.text = "forward"
            //console.info("forward")
        } else {
            batteryText.text = "backward"
            //console.info("backward")
        }
        setHeading()
    }

    function setDeclination(d) {
        declination = d
        declinationText.text = Number(d).toLocaleString(Qt.locale(), 'f', 2)
        setHeading()
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
                        text: "Dip Angle:  "
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

                    Text {
                        text: "Gravity:"
                    }

                    TextField {
                        id: gravityText
                        text: "1.0"
                        horizontalAlignment: TextInput.AlignRight
                        readOnly: true
                    }
                }
               
            }
             Item {
            Layout.preferredHeight: 5
        }

        GroupBox {

                    Layout.alignment: Qt.AlignHCenter
                    //border.color: "black"
                    //border.width: 1
                    title: "Configuration"
                GridLayout {

                    id: configurationgrid
                    //anchors.fill: parent
                    columns: 2 // Defines a grid with 2 columns
                    rowSpacing: 5
                    columnSpacing: 5
                    Layout.alignment: Qt.AlignHCenter

                    Text {
                        text: "Declination:"
                    }

                    TextField {
                        id: declinationText
                        text: "0.0"
                        horizontalAlignment: TextInput.AlignRight
                        readOnly: true
                    }

                    Text {
                        text: "Battery:"
                    }

                    TextField {
                        id: batteryText
                        text: "forward"
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
