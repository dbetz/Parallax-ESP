<html>
<head>
  <title>Parallax Wi-Fi Module Configuration - Wi-Fi Connection</title>
  <meta content="width=device-width, initial-scale=1" name="viewport">
  <link rel="stylesheet" type="text/css" href="style.css">
  <script type="text/javascript" src="140medley.min.js"></script>
  <script type="text/javascript">

    var ipAddressCtl;
    var xhr=j();
    var currAp="%wifi-ssid%";

    function createInputForAp(ap) {
      if (ap.essid=="" && ap.rssi==0) return;
      var div=document.createElement("div");
      div.id="apdiv";
      var rssi=document.createElement("div");
      var rssiVal=-Math.floor(ap.rssi/51)*32;
      rssi.className="icon";
      rssi.style.backgroundPosition="0px "+rssiVal+"px";
      var encrypt=document.createElement("div");
      var encVal="-64"; //assume wpa/wpa2
      if (ap.enc=="0") encVal="0"; //open
      if (ap.enc=="1") encVal="-32"; //wep
      encrypt.className="icon";
      encrypt.style.backgroundPosition="-32px "+encVal+"px";
      var input=document.createElement("input");
      input.type="radio";
      input.name="essid";
      input.value=ap.essid;
      if (currAp==ap.essid) input.checked="1";
      input.id="opt-"+ap.essid;
      var label=document.createElement("label");
      label.htmlFor="opt-"+ap.essid;
      label.textContent=ap.essid;
      div.appendChild(input);
      div.appendChild(rssi);
      div.appendChild(encrypt);
      div.appendChild(label);
      return div;
    }

    function getSelectedEssid() {
      var e=document.forms.wifiform.elements;
      for (var i=0; i<e.length; i++) {
        if (e[i].type=="radio" && e[i].checked) return e[i].value;
      }
      return currAp;
    }

    function scanAPs() {
      xhr.open("GET", "wifiscan.cgi");
      xhr.onreadystatechange=function() {
        if (xhr.readyState==4 && xhr.status>=200 && xhr.status<300) {
          var data=JSON.parse(xhr.responseText);
          currAp=getSelectedEssid();
          if (data.result.inProgress=="0" && data.result.APs.length>1) {
            $("#aps").innerHTML="";
            for (var i=0; i<data.result.APs.length; i++) {
              if (data.result.APs[i].essid=="" && data.result.APs[i].rssi==0) continue;
              $("#aps").appendChild(createInputForAp(data.result.APs[i]));
            }
            window.setTimeout(scanAPs, 20000);
          } else {
            window.setTimeout(scanAPs, 1000);
          }
        }
      }
      xhr.send();
    }

    window.onload=function(e) {
      scanAPs();
    };

  </script>
</head>

<body>
<div id="page">
  <div id="header">
    <h1>Wi-Fi Networks</h1>
  </div>
  <div id="main" class="clearfix">
    <div id="content">
      <table>
        <tr>
          <td>Module name:</td>
          <td>%module-name%</td>
        </tr>
        <tr>
          <td>Wi-Fi Mode:</td>
          <td>%wifi-mode%</td>
        </tr>
        <tr>
          <td>Station IP Address:</td>
          <td>%station-ipaddr%</td>
        </tr>
        <tr>
          <td>Station MAC Address:</td>
          <td>%station-macaddr%</td>
        </tr>
        <tr>
          <td>SoftAP IP Address:</td>
          <td>%softap-ipaddr%</td>
        </tr>
        <tr>
          <td>SoftAP MAC Address:</td>
          <td>%softap-macaddr%</td>
        </tr>
      </table>
      <p>Note: %wifi-ap-warning%</p>
      <form name="wifiform" action="connect.cgi" method="post">
        <p>
          Select a network from the list, enter password (if needed) in field below and click connect.<br>
        <div id="aps">Scanning...</div><br>
        Wi-Fi password(if needed):<br>
        <input type="text" name="passwd"><br>
        <input type="submit" name="connect" value="Connect!">
        </p>
      </form>
    </div>
    <nav id='navigation'>
      <input type="checkbox" id="resp" /><label for="resp"></label>
      <ul>
        <li><a href="../index.tpl">Home</a></li>
        <li><a href="wifi.tpl">Networks</a></li>
        <li><a href="../update-ffs.html">Files</a></li>
        <li><a href="../settings.html">Settings</a></li>
        <li><a href="../flash/index.html">Firmware</a></li>
      </ul>
    </nav>
  </div>
  <div id="ack"></div>
  <div id="footer">
    <a href="https://www.parallax.com">
      <img src="../logo.png">
    </a>
  </div>
</div>

</body>
</html>