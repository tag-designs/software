import QtQuick
import QtQuick.Controls.macOS
import QtQuick.Controls
import QtQuick3D
import QtQuick3D.AssetUtils
import QtQuick3D.Helpers
import QtQuick.Scene3D
import QtQuick.Layouts

Rectangle  {
    id: myMethod
    border.color: "black"
    border.width: 1
    anchors.fill: parent
    color: "#DEDEDE"
    //color: "#fffdf4"

    function setRotationQuaternion(qt) {
        //if (attitude.batteryForward) {
            attitude.rotation = qt
       // } else {
       //     attitude.rotation = qt.inverted()
        //}
        //console.info("setRotationQuaternion Called")
    }

    function setOrientation(h,p,r,d,f) {
        if (!compass.batteryForward) {
            h = (h + 180)%360
            p = -p
            r = -r
        }
        compass.rotation = -h
        compass.heading = Number(h).toLocaleString(Qt.locale(), 'f', 0)
        compass.pitch = Number(p).toLocaleString(Qt.locale(), 'f', 0)
        compass.roll = Number(r).toLocaleString(Qt.locale(), 'f', 0)
        compass.dip = Number(d).toLocaleString(Qt.locale(), 'f', 0)
        compass.field = Number(f).toLocaleString(Qt.locale(), 'f', 0)
        console.info("setoreintation called");
    }



    RowLayout {
        anchors.fill: parent

        //Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter

        spacing: 1
        Item {

             //This acts as a flexible horizontal spacer
            Layout.fillWidth: true
        }

        MyCompass {
            id: compass
            //Layout.preferredWidth: 300
            Layout.margins: 10
        }

       // Item {
            // This acts as a flexible horizontal spacer
        //    Layout.fillWidth: true
       // }

        MyAttitude {
            id: attitude
            //Layout.preferredWidth: 300
            Layout.margins: 10
        }

        Item {
             //This acts as a flexible horizontal spacer
            Layout.fillWidth: true
        }
    }
}
