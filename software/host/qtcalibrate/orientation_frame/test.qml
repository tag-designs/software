import QtQuick
import QtQuick.Controls.macOS
import QtQuick.Controls
import QtQuick3D
import QtQuick3D.AssetUtils
import QtQuick3D.Helpers
import QtQuick.Scene3D
import QtQuick.Layouts


ApplicationWindow { // The main window of the application
    visible: true // Makes the window visible
    width: 640 // Sets the initial width of the window
    height: 480 // Sets the initial height of the window
   


   RowLayout {
        anchors.fill: parent

        //Layout.alignment: Qt.AlignVCenter | Qt.AlignHCenter

        spacing: 1
        Item {

             //This acts as a flexible horizontal spacer
            Layout.fillWidth: true
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


