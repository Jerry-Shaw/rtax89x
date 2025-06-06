﻿<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<meta http-equiv="X-UA-Compatible" content="IE=Edge"/>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<meta HTTP-EQUIV="Pragma" CONTENT="no-cache">
<meta HTTP-EQUIV="Expires" CONTENT="-1">
<link rel="shortcut icon" href="images/favicon.png">
<link rel="icon" href="images/favicon.png">
<title>Untitled Document</title>
<link href="../NM_style.css" rel="stylesheet" type="text/css">
<link rel="stylesheet" type="text/css" href="../form_style.css">
<script type="text/javascript" src="/js/jquery.js"></script>
<script type="text/javascript" src="/help.js"></script>
<script type="text/javascript" src="/state.js"></script>
<script type="text/javascript" src="../js/httpApi.js"></script>
<script>
if(parent.location.pathname.search("index") === -1) top.location.href = "../"+'<% networkmap_page(); %>';

<% login_state_hook(); %>

var faq_href = "https://nw-dlcdnet.asus.com/support/forward.html?model=&type=Faq&lang="+ui_lang+"&kw=&num=157";
var faq_href1 = "https://nw-dlcdnet.asus.com/support/forward.html?model=&type=Faq&lang="+ui_lang+"&kw=&num=146";
var faq_href2 = "https://nw-dlcdnet.asus.com/support/forward.html?model=&type=Faq&lang="+ui_lang+"&kw=&num=147";
var faq_href3 = "https://nw-dlcdnet.asus.com/support/forward.html?model=&type=Faq&lang="+ui_lang+"&kw=&num=148";

function initial(){
	showtext(document.getElementById("printerModel"), parent.usbPorts[parent.currentUsbPort].deviceName);
	
	if(parent.usbPorts[parent.currentUsbPort].deviceName != ""
			&& parent.usbPorts[parent.currentUsbPort].serialNum == "<% nvram_get("u2ec_serial"); %>")
	{
		showtext(document.getElementById("printerStatus"), '<#CTL_Enabled#>');
		document.getElementById("printer_button").style.display = "";
		document.getElementById("button_descrition").style.display = "";
	}
	else{
		showtext(document.getElementById("printerStatus"), '<#CTL_Disabled#>');
		document.getElementById("printer_button").style.display = "none";
		document.getElementById("button_descrition").style.display = "none";
	}

	if('<% nvram_get("mfp_ip_monopoly"); %>' != "" && '<% nvram_get("mfp_ip_monopoly"); %>' != login_ip_str()){
		document.getElementById("monoBtn").style.display = "none";
		document.getElementById("monoDesc").style.display = "";
 		document.getElementById("monoP").style.width = "";
 		document.getElementById("monoP").style.float = "";
	}
	else{
		document.getElementById("monoBtn").style.display = "";
		document.getElementById("monoDesc").style.display = "none";
	}
	document.getElementById("faq").href=faq_href;
	document.getElementById("faq1").href=faq_href1;
	document.getElementById("faq2").href=faq_href2;
	document.getElementById("faq3").href=faq_href3;
}

function cleanTask(){
	parent.showLoading(2);
	FormActions("/apply.cgi", "mfp_monopolize", "", "");
	document.form.submit();
}
</script>
</head>

<body class="statusbody" onload="initial();">

<form method="post" name="form" action="/start_apply.htm">
<input type="hidden" name="productid" value="<% nvram_get("productid"); %>">
<input type="hidden" name="current_page" value="device-map/printer.asp">
<input type="hidden" name="next_page" value="device-map/printer.asp">
<input type="hidden" name="modified" value="0">
<input type="hidden" name="action_mode" value="">
<input type="hidden" name="action_script" value="">
<input type="hidden" name="action_wait" value="2">
<input type="hidden" name="preferred_lang" id="preferred_lang" value="<% nvram_get("preferred_lang"); %>">

<table width="95%" border="1" align="center" cellpadding="4" cellspacing="0" bordercolor="#6b8fa3" class="table1px">
	<tr>
		<td style="padding:5px 10px 5px 15px;">
			<p class="formfonttitle_nwm"><#PrinterStatus_x_PrinterModel_itemname#></p>
			<p style="padding-left:10px; margin-top:3px; background-color:#444f53; line-height:20px;" id="printerModel"></p>
			<div style="margin-top:5px;" class="line_horizontal"></div>
		</td>
	</tr>  

	<tr>
		<td style="padding:5px 10px 5px 15px;">
			<p class="formfonttitle_nwm"><#Printing_status#></p>
			<p style="padding-left:10px; margin-top:3px; background-color:#444f53; line-height:20px;" id="printerStatus"></p>
			<div style="margin-top:5px;" class="line_horizontal"></div>
		</td>
	</tr>  

	<tr id="printer_button" style="display:none;">
		<td style="padding:5px 10px 5px 15px;">
			<p class="formfonttitle_nwm" id="monoP" style="width:138px;"><#Printing_button_item#></p>
			<input id="monoBtn" type="button" class="button_gen" value="<#WLANConfig11b_WirelessCtrl_button1name#>" onclick="cleanTask();">
			<p id="monoDesc" style="padding-left:10px; margin-top:3px; background-color:#444f53; line-height:20px;"><% nvram_get("mfp_ip_monopoly"); %></p>
			<div style="margin-top:5px;" class="line_horizontal"></div>
		</td>
	</tr>  
</table>

<div id="button_descrition" style="display:none;padding:5px 0px 5px 25px;">
<ul style="font-size:11px; font-family:Arial; color:#FFF; padding:0px; margin:0px; list-style:outside; line-height:150%;">
	<li><#PrinterStatus_x_Monopoly_itemdesc#> <a id="faq" href="" target="_blank" style="text-decoration:underline;">FAQ</a></li>
	<li>
		<a id="faq1" href="" target="_blank" style="text-decoration:underline;"><#asus_ez_print_share#> FAQ</a>
		<br>
		<a href="http://dlcdnet.asus.com/pub/ASUS/LiveUpdate/Release/Wireless/Printer.zip" style="text-decoration:underline;font-weight:bolder;color:#FC0"><#Download_now#></a>
	</li>
	<li>
		<a id="faq2" href="" target="_blank" style="text-decoration:underline;"><#LPR_print_share#> FAQ (Windows)</a>
	</li>
	<li>
		<a id="faq3" href="" target="_blank" style="text-decoration:underline;"><#LPR_print_share#> FAQ (MAC)</a>
	</li>
</ul>
</div>
</form>
</body>
</html>
