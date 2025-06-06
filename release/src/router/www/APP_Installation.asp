﻿<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<html xmlns:v>
<head>
<meta http-equiv="X-UA-Compatible" content="IE=Edge"/>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8">
<meta HTTP-EQUIV="Pragma" CONTENT="no-cache">
<meta HTTP-EQUIV="Expires" CONTENT="-1">
<link rel="shortcut icon" href="images/favicon.png">
<link rel="icon" href="images/favicon.png"><title><#Web_Title#> - <#Menu_usb_application#></title>
<link rel="stylesheet" type="text/css" href="index_style.css">
<link rel="stylesheet" type="text/css" href="form_style.css">
<link rel="stylesheet" type="text/css" href="usp_style.css">
<link rel="stylesheet" type="text/css" href="app_installation.css">
<script type="text/javascript" src="/js/jquery.js"></script>
<script type="text/javascript" src="/state.js"></script>
<script type="text/javascript" src="/popup.js"></script>
<script type="text/javascript" src="/help.js"></script>
<script type="text/javascript" src="/disk_functions.js"></script>
<script type="text/javascript" src="/js/httpApi.js"></script>
<style>
#Aidisk_png{
	background-position: 0% 0%;
}
#server_png{
	background-position: 0% 12.5%;
}
#PrinterServer_png{
	background-position: 0% 25%;
}
#modem_png{
	background-position: 0% 100%;
}
#downloadmaster_png{
	background-position: 0% 50.5%;
}
#mediaserver_png{
	background-position: 0% 88%;
}
#mediaserver2_png{
	background-position: 0% 88%;
}
#aicloud_png{
	background-position: 0% 75%;
}
#TimeMachine_png{
	background-position: 0% 37.5%;
}
#fileflex_png{
	background-position: 0% 63%;
}
#DownloadAcceleration_png{
	background-position: 0% 50.5%;
}
.app_list{
	position: relative;
	background-image: url(images/New_ui/USBExt/app_list_active.svg);
	background-size: 90%;
	background-repeat: no-repeat;
	background-position: 0% 0%;
	width: 45px;
	height: 42px;
}
.app_list::before{
	content: "";
	position: absolute;
	top: -18px;
	right: -15px;
	background-image: url(images/New_ui/USBExt/circle.svg);
	background-size: cover;
	background-position: 0% 100%;
	width: 80px;
	height: 80px;
}

.perNode_app_table{
	width: 740px;
	position: absolute;
	left: 50%;
	margin-top: 30px;
	margin-left: -370px;
}

.perNode_nohover:hover{
	background-color: #21333e;
	*background-color: #21333e;
}
</style>
<script>
var apps_array = <% apps_info("asus"); %>;
<% apps_state_info(); %>
var apps_download_percent_done = 0;
<% apps_action(); %> //trigger apps_action.

var stoppullstate = 0;
var isinstall = 0;
var installPercent = 1;
var default_apps_array = new Array();
var appnum = 0;
var _appname = "";
var _dm_install;
var _dm_enable;
var dm_http_port = '<% nvram_get("dm_http_port"); %>';
if(dm_http_port == "")
	dm_http_port = "8081";

var _apps_action = '<% get_parameter("apps_action"); %>';
if(_apps_action == 'cancel')
	_apps_action = '';

var webs_state_update;
var webs_state_error;
var webs_state_info;
var wan_unit_orig = '<% nvram_get("wan_unit"); %>';
var fileflex_text = "<#FileFlex_desc0#>";

var faq_href1 = "https://nw-dlcdnet.asus.com/support/forward.html?model=&type=Faq&lang="+ui_lang+"&kw=&num=141";
var faq_href2 = "https://nw-dlcdnet.asus.com/support/forward.html?model=&type=Faq&lang="+ui_lang+"&kw=&num=142";

function initial(){
	default_apps_array = [["AiDisk", "aidisk.asp", "<#AiDiskWelcome_desp1#>", "Aidisk_png", ""],
			["<#Servers_Center#>", "mediaserver.asp", "<#UPnPMediaServer_Help#>", "server_png", ""],
			["<#Network_Printer_Server#>", "PrinterServer.asp", "<#Network_Printer_desc#>", "PrinterServer_png", ""],
			["3G/4G", "Advanced_Modem_Content.asp", "<#HSDPAConfig_hsdpa_enable_hint1#>", "modem_png", ""],
			["<#TimeMach#>", "Advanced_TimeMachine.asp", "<#TimeMach_enable_hint#>", "TimeMachine_png", "1.0.0.1"],
			["Tencent Download Acceleration", "Advanced_TencentDownloadAcceleration.asp", "Tencent Download Acceleration", "DownloadAcceleration_png", "1.0.0.1"]];

	if(re_mode == "1"){
		$("#FormTitle").addClass("perNode_app_table");
		default_apps_array[1][1] = "";
		$(".submenuBlock").css("margin-top", "initial");
	}
	else{
		$("#content_table").addClass("content");
		$("#FormTitle").addClass("app_table app_table_usb");
		show_menu();
	}

	$("#FormTitle").css("display", "");

	if(!media_support){
		default_apps_array[1][1] = "Advanced_AiDisk_samba.asp";
		default_apps_array[1].splice(2,1,"<#MediaServer_Help#>");
	}

	if(sw_mode == 2 || sw_mode == 3 || sw_mode == 4 || re_mode == "1" || noaidisk_support){
		if(default_apps_array.getIndexByValue2D("AiDisk") != -1)
			default_apps_array = default_apps_array.del(default_apps_array.getIndexByValue2D("AiDisk")[0]);
	}

	if(!printer_support || noprinter_support || re_mode == "1"){
		if(default_apps_array.getIndexByValue2D("<#Network_Printer_Server#>") != -1)
			default_apps_array = default_apps_array.del(default_apps_array.getIndexByValue2D("<#Network_Printer_Server#>")[0]);
	}

	if(sw_mode == 2 || sw_mode == 3 || sw_mode == 4 || re_mode == "1" || !modem_support  || nomodem_support ||
		based_modelid.substring(0,3) == "4G-"){
		if(default_apps_array.getIndexByValue2D("3G/4G") != -1)
			default_apps_array = default_apps_array.del(default_apps_array.getIndexByValue2D("3G/4G")[0]);
	}

	if(!timemachine_support){
		if(default_apps_array.getIndexByValue2D("<#TimeMach#>") != -1)
			default_apps_array = default_apps_array.del(default_apps_array.getIndexByValue2D("<#TimeMach#>")[0]);
	}

	if(!tencent_game_acc_support){
		if(default_apps_array.getIndexByValue2D("Tencent Download Acceleration") != -1)
			default_apps_array = default_apps_array.del(default_apps_array.getIndexByValue2D("Tencent Download Acceleration")[0]);
	}

	trNum = default_apps_array.length;

	if(_apps_action == '' && 
		(apps_state_upgrade == 4 || apps_state_upgrade == "") && 
		(apps_state_enable == 2 || apps_state_enable == "") &&
		(apps_state_update == 2 || apps_state_update == "") && 
		(apps_state_remove == 2 || apps_state_remove == "") &&
		(apps_state_switch == 5 || apps_state_switch == "") && 
		(apps_state_autorun == 4 || apps_state_autorun == "") && 
		(apps_state_install == 5 || apps_state_install == "")){
		setTimeout(show_apps, 500);
	}
	else{
		setTimeout(update_appstate, 2000);
	}

	setTimeout(function(){
		document.getElementById("faq").href=faq_href1;
		document.getElementById("faq2").href=faq_href2;
	}, 1000);
}

function update_appstate(e){
  $.ajax({
    url: '/update_appstate.asp',
    dataType: 'script',
	
    error: function(xhr){
      update_appstate();
    },
    success: function(response){
			if(stoppullstate == 1)
				return false;
			else if(!check_appstate()){
      			setTimeout("update_appstate();", 1000);
			}
			else
      			setTimeout("update_applist();", 3000);
		}    
  });
}

function update_applist(e){
  $.ajax({
    url: '/update_applist.asp',
    dataType: 'script',
	
    error: function(xhr){
		update_applist();
    },
    success: function(response){
			if(isinstall > 0 && cookie.get("apps_last") == "downloadmaster"){
				for(var i = 0; i < apps_array.length; i++){
					if(apps_array[i][0] == "DM2_Utility")
						document.getElementById("DMUtilityLink").href = apps_array[i][5]+ "/" + apps_array[i][12];
						
					if(apps_array[i][0] == "downloadmaster"){			//set cookie for help.js	
						_dm_install = apps_array[i][3];
						_dm_enable = apps_array[i][4];
					}	
				}
				document.getElementById("isInstallDesc").style.display = "";
				setTimeout('divdisplayctrl("none", "none", "none", "");', 100);
				document.getElementById("return_btn").style.display = "";
			}
			else if(isinstall > 0 && cookie.get("apps_last") == "fileflex"){
				window.location.href = "fileflex.asp";
			}
			else{
				// setTimeout('show_partition();', 100);
				setTimeout('show_apps();', 100);
			}
		}    
  });
}

function check_appstate(){
	if(_apps_action != "" && apps_state_upgrade == "" && apps_state_enable == "" && apps_state_update == "" && 
		 apps_state_remove == "" && apps_state_switch == "" && apps_state_autorun == "" && apps_state_install == ""){
		return false;
	}

	if((apps_state_upgrade == 4 || apps_state_upgrade == "") && (apps_state_enable == 2 || apps_state_enable == "") &&
		(apps_state_update == 2 || apps_state_update == "") && (apps_state_remove == 2 || apps_state_remove == "") &&
		(apps_state_switch == 5 || apps_state_switch == "") && (apps_state_autorun == 4 || apps_state_autorun == "") && 
		(apps_state_install == 5 || apps_state_install == "")){
		if(apps_state_install == 5 || apps_state_upgrade == 4){
			if(installPercent > 1 && installPercent < 95)
				installPercent = 95;
			else
				return true;
		}
		else
			return true;
	}

	var errorcode;
	var proceed = 0.6;

	if(apps_state_upgrade != 4 && apps_state_upgrade != ""){ // upgrade error handler
		errorcode = "apps_state_upgrade = " + apps_state_upgrade;
		if(apps_state_error == 1)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_inputerror#>";
		else if(apps_state_error == 2)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_mount#>";
		else if(apps_state_error == 4)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_install#>";
		else if(apps_state_error == 6)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_remote_responding#>";
		else if(apps_state_error == 7)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_upgrade#>";
		else if(apps_state_error == 9)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_unmount#>";
		else if(apps_state_error == 10)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_dev_responding#>";
		else if(apps_state_upgrade == 0)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_initializing#>";
		else if(apps_state_upgrade == 1){
			if(apps_download_percent > 0 && apps_download_percent <= 100){
				document.getElementById("apps_state_desc").innerHTML = apps_download_file + " is downloading.. " + " <b>" + apps_download_percent + "</b> <span style='font-size: 16px;'>%</span>";
				apps_download_percent_done = 0;
			}
			else if(apps_download_percent_done > 5){
				if(installPercent > 99)
					installPercent = 99;
				document.getElementById("loadingicon").style.display = "none";
				document.getElementById("apps_state_desc").innerHTML = "[" + cookie.get("apps_last") + "] " + "<#Excute_processing#> <b>" + Math.round(installPercent) +"</b> <span style='font-size: 16px;'>%</span>";
				installPercent = installPercent + proceed;//*/
			}
			else{
				document.getElementById("apps_state_desc").innerHTML = "&nbsp;<#usb_initializing#>...";
				apps_download_percent_done++;
			}
		}
		else if(apps_state_upgrade == 2)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_uninstalling#>";
		else{
			if(apps_depend_action_target != "terminated" && apps_depend_action_target != "error"){
				if(apps_depend_action_target == "")
					document.getElementById("apps_state_desc").innerHTML = "<b>[" + cookie.get("apps_last") + "] " + "<#Excute_processing#> </b>";
				else
					document.getElementById("apps_state_desc").innerHTML = "<b>[" + cookie.get("apps_last") + "] " + "<#Excute_processing#> </b>"
							+"<br> <span style='font-size: 16px;'> <#Excute_processing#>："+apps_depend_do+"</span>"
							+"<br> <span style='font-size: 16px;'>"+apps_depend_action+"  "+apps_depend_action_target+"</span>"
							;
			}
			else{
				if(installPercent > 99)
					installPercent = 99;
				document.getElementById("loadingicon").style.display = "none";
				document.getElementById("apps_state_desc").innerHTML = "[" + cookie.get("apps_last") + "] " + "<#Excute_processing#> <b>" + Math.round(installPercent) +"</b> <span style='font-size: 16px;'>%</span>";
				installPercent = installPercent + proceed;
			}
		}
	}
	else if(apps_state_enable != 2 && apps_state_enable != ""){
		errorcode = "apps_state_enable = " + apps_state_enable;
		if(apps_state_error == 1)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_unknown#>";
		else if(apps_state_error == 2)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_mount#>";
		else if(apps_state_error == 3)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_create_swap#>";
        else if(apps_state_error == 8)
            document.getElementById("apps_state_desc").innerHTML = "Enable error!";
		else{
			document.getElementById("loadingicon").style.display = "";
			document.getElementById("apps_state_desc").innerHTML = "<#QIS_autoMAC_desc2#>";
		}
	}
	else if(apps_state_update != 2 && apps_state_update != ""){
		errorcode = "apps_state_update = " + apps_state_update;
		if(apps_state_error == 1)
			document.getElementById("apps_state_desc").innerHTML = "<#USB_Application_Preparing#>";
		else if(apps_state_error == 2)
			document.getElementById("apps_state_desc").innerHTML = "<#USB_Application_No_Internet#>";
		else
			document.getElementById("apps_state_desc").innerHTML = "<#upgrade_processing#>";
	}
	else if(apps_state_remove != 2 && apps_state_remove != ""){
		errorcode = "apps_state_remove = " + apps_state_remove;
		document.getElementById("apps_state_desc").innerHTML = "<#uninstall_processing#>";
	}
	else if(apps_state_switch != 4 && apps_state_switch != 5 && apps_state_switch != ""){
		errorcode = "apps_state_switch = " + apps_state_switch;
		if(apps_state_error == 1)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_unknown#>";
		else if(apps_state_error == 2)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_mount#>";
		else if(apps_state_switch == 1)
			document.getElementById("apps_state_desc").innerHTML = "<#USB_Application_Stopping#>";
		else if(apps_state_switch == 2)
			document.getElementById("apps_state_desc").innerHTML = "<#USB_Application_Stopwapping#>";
		else if(apps_state_switch == 3)
			document.getElementById("apps_state_desc").innerHTML = "<#USB_Application_Partition_Check#>";
		else
			document.getElementById("apps_state_desc").innerHTML = "<#Excute_processing#>";
	}
	else if(apps_state_autorun != 4 && apps_state_autorun != ""){
		errorcode = "apps_state_autorun = " + apps_state_autorun;
		if(apps_state_error == 1)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_unknown#>";
		else if(apps_state_error == 2)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_mount#>";
		else if(apps_state_autorun == 1)
			document.getElementById("apps_state_desc").innerHTML = "<#USB_Application_disk_checking#>";
		else if(apps_state_install == 2)
			document.getElementById("apps_state_desc").innerHTML = "<#USB_Application_Swap_creating#>";
		else
			document.getElementById("apps_state_desc").innerHTML = "<#Auto_Install_processing#>";
	}
	else if(apps_state_install != 5 && apps_state_error > 0){ // install error handler
		if(apps_state_error == 1)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_inputerror#>";
		else if(apps_state_error == 2)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_mount#>";
		else if(apps_state_error == 3)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_create_swap#>";
		else if(apps_state_error == 4)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_install#>";
		else if(apps_state_error == 5)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_connect_internet#>";
		else if(apps_state_error == 6)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_remote_responding#>";
		else if(apps_state_error == 7)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_upgrade#>";
		else if(apps_state_error == 9)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_unmount#>";
		else if(apps_state_error == 10)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_failed_dev_responding#>";

		isinstall = 0;
	}
	else if(apps_state_install != 5 && apps_state_install != ""){
		isinstall = 1;
		errorcode = "_apps_state_install = " + apps_state_install;

		if(apps_state_install == 0)
			document.getElementById("apps_state_desc").innerHTML = "<#usb_partitioning#>";
		else if(apps_state_install == 1)
			document.getElementById("apps_state_desc").innerHTML = "<#USB_Application_disk_checking#>";
		else if(apps_state_install == 2)
			document.getElementById("apps_state_desc").innerHTML = "<#USB_Application_Swap_creating#>";
		else if(apps_state_install == 3){
			if(apps_download_percent > 0 && apps_download_percent <= 100){
				document.getElementById("apps_state_desc").innerHTML = apps_download_file + " is downloading.. " + " <b>" + apps_download_percent + "</b> <span style='font-size: 16px;'>%</span>";
				apps_download_percent_done = 0;
			}
			else if(apps_download_percent_done > 5){
				if(installPercent > 99)
					installPercent = 99;
				document.getElementById("loadingicon").style.display = "none";
				document.getElementById("apps_state_desc").innerHTML = "[" + cookie.get("apps_last") + "] " + "<#Excute_processing#> <b>" + Math.round(installPercent) +"</b> <span style='font-size: 16px;'>%</span>";
				installPercent = installPercent + proceed;//*/
			}
			else{
				document.getElementById("apps_state_desc").innerHTML = "&nbsp;<#usb_initializing#>...";
				apps_download_percent_done++;
			}
		}
		else{ //apps_state_install == 4
			if(apps_depend_action_target != "terminated" && apps_depend_action_target != "error"){
				if(apps_depend_action_target == ""){
					if(installPercent > 99)
						installPercent = 99;
					document.getElementById("loadingicon").style.display = "none";
					document.getElementById("apps_state_desc").innerHTML = "[" + cookie.get("apps_last") + "] " + "<#Excute_processing#> <b>" + Math.round(installPercent) +"</b> <span style='font-size: 16px;'>%</span>";
					installPercent = installPercent + proceed;
				}
				else{
					var _apps_depend_do = apps_depend_do.replace(apps_depend_action, "<span style='color:#FC0'>"+apps_depend_action+"</span>");

					document.getElementById("apps_state_desc").innerHTML = "<b>[" + cookie.get("apps_last") + "] " + "<#Excute_processing#> </b>"
							+"<br> <span style='font-size: 16px;'> <#Excute_processing#>："+_apps_depend_do+"</span>"
							+"<br><br> <span style='font-size: 18px;'>"+apps_depend_action+"  "+apps_depend_action_target+"</span>"
					;
				}
			}
			else{
				if(installPercent > 99)
					installPercent = 99;
				document.getElementById("loadingicon").style.display = "none";
				document.getElementById("apps_state_desc").innerHTML = "[" + cookie.get("apps_last") + "] " + "<#Excute_processing#> <b>" + Math.round(installPercent) +"</b> <span style='font-size: 16px;'>%</span>";
				installPercent = installPercent + proceed;
			}
		}
	}
	else{
		document.getElementById("loadingicon").style.display = "";
		document.getElementById("apps_state_desc").innerHTML = "<#QIS_autoMAC_desc2#>";
	}
	
	if(apps_state_error != 0){
		document.getElementById("return_btn").style.display = "";
		document.getElementById("loadingicon").style.display = "none";
		stoppullstate = 1;
	}
	else
		document.getElementById("return_btn").style.display = "none";

	document.getElementById("cancelBtn").style.display = "";
	return false;
}

var trNum;
function show_apps(){
	if(re_mode != "1")
		document.getElementById("usbHint").innerHTML = "<#remove_usb_hint#>";

	var counter = 0;
	appnum = 0;

	if(apps_array == "" && (appnet_support || appbase_support)){
		apps_array = [["downloadmaster", "", "", "no", "no", "", "", "<#DM_EnableHint#>", "downloadmaster_png", "", "", ""],
									["mediaserver", "", "", "no", "no", "", "", "", "mediaserver_png", "", "", ""]];

		if(aicloudipk_support)
			apps_array.push(["aicloud", "", "", "no", "no", "", "", "AiCloud 2.0 utilities", "aicloud_png", "", "", ""]);

		if(fileflex_support)
			apps_array.push(["fileflex", "", "", "no", "no", "", "", fileflex_text, "downloadmaster_png", "", "", ""]);
	}

	if(!aicloudipk_support){
		var aicloud_idx = apps_array.getIndexByValue2D("aicloud");
		if(aicloud_idx[1] != -1 && aicloud_idx != -1)
			apps_array.splice(aicloud_idx[0], 1);
	}

	if(nodm_support){
		var dm_idx = apps_array.getIndexByValue2D("downloadmaster");
		if(dm_idx[1] != -1 && dm_idx != -1)
			apps_array.splice(dm_idx[0], 1);
	}

	if(media_support || nomedia_support){ // buildin or hidden 
		// remove mediaserver
		var media_idx = apps_array.getIndexByValue2D("mediaserver");
		if(media_idx[1] != -1 && media_idx != -1)
			apps_array.splice(media_idx[0], 1);

		// remove mediaserver2
		var media2_idx = apps_array.getIndexByValue2D("mediaserver2");
		if(media2_idx[1] != -1 && media2_idx != -1)
			apps_array.splice(media2_idx[0], 1);
	}
	else{
		var media_idx = apps_array.getIndexByValue2D("mediaserver2");

		if(media_idx[1] != -1 && media_idx != -1)
			apps_array.splice(media_idx[0], 1);

		var media_idx = apps_array.getIndexByValue2D("mediaserver");
		if(media_idx == -1 || media_idx[1] == -1){
			var apps_len = apps_array.length;
			apps_array[apps_len] = ["mediaserver", "", "", "no", "no", "", "", "", "mediaserver_png", "", "", ""];
		}
	}

	if(!fileflex_support || re_mode == "1"){
		var fileflex_idx = apps_array.getIndexByValue2D("fileflex");
		if(fileflex_idx[1] != -1 && fileflex_idx != -1)
			apps_array.splice(fileflex_idx[0], 1);
	}
	else{
		var fileflex_idx = apps_array.getIndexByValue2D("fileflex");
		if(fileflex_idx[1] != -1 && fileflex_idx != -1){
			apps_array[fileflex_idx[0]][7] = fileflex_text;
		}
	}

	// calculate div height
	htmlcode = '<table class="appsTable" align="center" style="margin:auto;border-collapse:collapse;">';
	
	//show default Apps
	for(var i = 0; i < default_apps_array.length; i++){
		htmlcode += '<tr><td align="center" class="app_table_radius_left" style="width:85px">';
		//Viz modified to CSS sprites : htmlcode += '<img style="margin-top:0px;" src="/images/New_ui/USBExt/'+ default_apps_array[i][3] +'" style="cursor:pointer" onclick="location.href=\''+ default_apps_array[i][1] +'\';">';
		if(i == 3 && wan_unit_orig != usb_index && usb_index != -1){
			htmlcode += '<div id="'+default_apps_array[i][3]+'" class="app_list" style="cursor:pointer" onclick="go_modem_page('+usb_index+');"></div>';
		}
		else
			htmlcode += '<div id="'+default_apps_array[i][3]+'" class="app_list" style="cursor:pointer" onclick="location.href=\''+ default_apps_array[i][1] +'\';"></div>';
		htmlcode += '</td><td class="app_table_radius_right" style="width:350px;">\n';
		if(i == 3 && wan_unit_orig != usb_index && usb_index != -1){
			console.log("2 need to change wan unit!");
			htmlcode += '<div class="app_name"><a style="text-decoration: underline; cursor:pointer;" onclick="go_modem_page('+usb_index+');">'+ default_apps_array[i][0] + '</a></div>\n';
		}
		else{
			if(default_apps_array[i][1] != "")
				htmlcode += '<div class="app_name"><a style="text-decoration: underline;" href="' + default_apps_array[i][1] + '">' + default_apps_array[i][0] + '</a></div>\n';
			else
				htmlcode += '<div class="app_name">' + default_apps_array[i][0] + '</div>\n';
		}
		if(i ==3){
			htmlcode += '<div class="app_desc">' + default_apps_array[i][2] + ' <a href="https://www.asus.com/event/networks_3G4G_support/" target="_blank" style="text-decoration:underline;"><#Support#></a></div>\n';
		}
		else{
			htmlcode += '<div class="app_desc">' + default_apps_array[i][2] + '</div>\n';
		}
		htmlcode += '<div style="margin-top:10px;"></div><br/><br/></td></tr>\n';
	}

	// show all apps
	for(var i = 0; i < apps_array.length; i++){
		if(apps_array[i][0] == "DM2_Utility")
			document.getElementById("DMUtilityLink").href = apps_array[i][5]+ "/" + apps_array[i][12];

		if(apps_array[i][0] != "downloadmaster" && apps_array[i][0] != "mediaserver" && apps_array[i][0] != "mediaserver2" && apps_array[i][0] != "aicloud" && apps_array[i][0] != "fileflex") // discard unneeded apps
			continue;
		else if((apps_array[i][0] == "downloadmaster" || apps_array[i][0] == "mediaserver" || apps_array[i][0] == "mediaserver2" || apps_array[i][0] == "aicloud" || apps_array[i][0] == "fileflex") && apps_array[i][3] == "yes" && apps_array[i][4] == "yes"){

			var header_info = [<% get_header_info(); %>];
			var host_name = header_info[0].host;
			apps_array[i][6] = "http://" + host_name + ":" + dm_http_port;

			if(apps_array[i][0] == "aicloud") // append URL
				apps_array[i][6] = "/cloud_main.asp";
			else if(apps_array[i][0] == "mediaserver" || apps_array[i][0] == "mediaserver2")
				apps_array[i][6] += "/mediaserverui/mediaserver.asp";
			else if(apps_array[i][0] == "fileflex")
				apps_array[i][6] = "https://asus.fileflex.com";
		}
		appnum++; // cal the needed height of applist table 

		if(apps_array[i][4] == "no" && apps_array[i][3] == "yes")
			apps_array[i][6] = "";
			
		// apps_icon
		htmlcode += '<tr style="height: 100px;"><td class="app_table_radius_left circle active" align="center" style="width:85px">\n';
		if(apps_array[i][0] == "fileflex"){
			if(apps_array[i][4] == "yes" && apps_array[i][3] == "yes") // enable
				htmlcode += '<div id="'+apps_array[i][0]+'_png" class="app_list" style="cursor:pointer" onclick="loginAcc();"></div>';
			else // uninstall or disable
				htmlcode += '<div id="'+apps_array[i][0]+'_png" class="app_list"></div>';
		}
		else{
			if(apps_array[i][4] == "yes" && apps_array[i][3] == "yes"){
				if(apps_array[i][6] != ""){
					htmlcode += '<div id="'+apps_array[i][0]+'_png" class="app_list" style="cursor:pointer" onclick="location.href=\''+ apps_array[i][6] +'\';"></div>';
				}
				else{
					htmlcode += '<div id="'+apps_array[i][0]+'_png" class="app_list"></div>';
				}
			}
			else{
				htmlcode += '<div id="'+apps_array[i][0]+'_png" class="app_list"></div>';
			}
		}
		htmlcode += '</td>\n';

		// apps_name
		htmlcode += '<td class="app_table_radius_right" style="width:350px;">\n';
		if(apps_array[i][0] == "downloadmaster")
			apps_array[i][0] = "<#DM_title#>";
		else if(apps_array[i][0] == "mediaserver" || apps_array[i][0] == "mediaserver2")
			apps_array[i][0] = "Media Server";
		else if(apps_array[i][0] == "aicloud")
			apps_array[i][0] = "AiCloud 2.0";
		else if(apps_array[i][0] == "fileflex")
			apps_array[i][0] = "FileFlex";

		if(apps_array[i][6] != ""){ // with hyper-link
			htmlcode += '<div class="app_name">';

			if(apps_array[i][1] == ""){
				if(apps_array[i][0] == "FileFlex"){
					if(apps_array[i][4] == "no" && apps_array[i][3] == "yes") // disable
						htmlcode += '<a style="color:gray;">' + apps_array[i][0] + '</a></div>\n';
					else // uninstall or enable
						htmlcode += '<a target="_blank" href="' + apps_array[i][6] + '" style="text-decoration:underline;">' + apps_array[i][0] + '</a></div>\n';
				}
				else{
					if(apps_array[i][3] == "no") // uninstall
						htmlcode += apps_array[i][0] + '</div>\n';
					else if(apps_array[i][4] == "no" && apps_array[i][3] == "yes") // disable
						htmlcode += '<a href="' + apps_array[i][6] + '" style="color:gray;">' + apps_array[i][0] + '<span class="app_ver" style="color:gray">' + apps_array[i][1] + '</span></a></div>\n';
					else{ // enable
						if(apps_array[i][0] == "<#DM_title#>")
							htmlcode += '<a target="_blank" href="' + apps_array[i][6] + '" style="text-decoration: underline;">' + apps_array[i][0] + '</a><span class="app_ver">' + apps_array[i][1] + '</span></div>\n';
						else
							htmlcode += '<a href="' + apps_array[i][6] + '" style="text-decoration: underline;">' + apps_array[i][0] + '</a><span class="app_ver">' + apps_array[i][1] + '</span></div>\n';
					}
				}
			}
			else{
				if(apps_array[i][0] == "FileFlex"){
					if(apps_array[i][4] == "no" && apps_array[i][3] == "yes") // disable
						htmlcode += '<a style="color:gray;">' + apps_array[i][0] + '</a><span class="app_ver" style="color:gray">ver. ' + apps_array[i][1] + '</span></div>\n';
					else // uninstall or enable
						htmlcode += '<a target="_blank" href="' + apps_array[i][6] + '" style="text-decoration: underline;">' + apps_array[i][0] + '</a><span class="app_ver">ver. ' + apps_array[i][1] + '</span></div>\n';
				}
				else{
					if(apps_array[i][4] == "no" && apps_array[i][3] == "yes") // disable
						htmlcode += '<a href="' + apps_array[i][6] + '" style="color:gray">' + apps_array[i][0] + '<span class="app_ver" style="color:gray">ver. ' + apps_array[i][1] + '</span></a></div>\n';
					else{ // enable
						if(apps_array[i][0] == "<#DM_title#>")
							htmlcode += '<a target="_blank" href="' + apps_array[i][6] + '" style="text-decoration: underline;">' + apps_array[i][0] + '</a><span class="app_ver">ver. ' + apps_array[i][1] + '</span></div>\n';
						else
							htmlcode += '<a href="' + apps_array[i][6] + '" style="text-decoration: underline;">' + apps_array[i][0] + '</a><span class="app_ver">ver. ' + apps_array[i][1] + '</span></div>\n';
					}
				}
			}
		}
		else{ // without hyper-link
			if(apps_array[i][4] == "no" && apps_array[i][3] == "yes")
				htmlcode += '<div class="app_name" style="color:gray">';
			else
				htmlcode += '<div class="app_name">';
			if(apps_array[i][1] == "")
				htmlcode += apps_array[i][0] + '<span class="app_ver">' + apps_array[i][1] + '</span></div>\n';
			else
				htmlcode += apps_array[i][0] + '<span class="app_ver">ver. ' + apps_array[i][1] + '</span></div>\n';
		}

		if(apps_array[i][0] == "<#DM_title#>")
			apps_array[i][0] = "downloadmaster";
		else if(apps_array[i][0] == "Media Server"){
			apps_array[i][0] = "mediaserver";
		}
		else if(apps_array[i][0] == "AiCloud 2.0")
			apps_array[i][0] = "aicloud";
		else if(apps_array[i][0] == "FileFlex")
			apps_array[i][0] = "fileflex";

		// apps_desc
		if(apps_array[i][4] == "no" && apps_array[i][3] == "yes"){
			htmlcode += '<div class="app_desc" style="color:gray">' + apps_array[i][7] + '</div>\n';
			htmlcode += '<div style="margin-top:10px;">\n';		
		}
		else{
			htmlcode += '<div class="app_desc">' + apps_array[i][7] + '</div>\n';
			htmlcode += '<div style="margin-top:10px;">\n';		
		}

		// apps_action
		if(apps_array[i][3] == "yes"){ //installed
			htmlcode += '<span class="app_action" onclick="apps_form(\'remove\',\''+ apps_array[i][0] +'\',\'\');">Uninstall</span>\n';	/* untranslated */
			if(apps_array[i][4] == "yes")		//enable
				htmlcode += '<span class="app_action" onclick="apps_form(\'enable\',\''+ apps_array[i][0] +'\',\'no\');"><#WLANConfig11b_WirelessCtrl_buttonname#></span>\n';
			else
				htmlcode += '<span class="app_action" onclick="apps_form(\'enable\',\''+ apps_array[i][0] +'\',\'yes\');"><#WLANConfig11b_WirelessCtrl_button1name#></span>\n';

			if(sw_mode == 3 || link_internet == "2")
				htmlcode += '<span class="app_action" onclick="apps_form(\'update\',\''+ apps_array[i][0] +'\',\'\');">Check update</span>\n';	/* untranslated */
	
			if(apps_array[i][0] == "downloadmaster"){
				htmlcode += '<span class="app_action" onclick="divdisplayctrl(\'none\', \'none\', \'none\', \'\');"><#CTL_help#></span>\n';
			}
			else if(apps_array[i][0] == "fileflex")
				htmlcode += '<span class="app_action" onclick="location.href=\'fileflex.asp\';"><#CTL_help#></span>\n';

			if(	cookie.get("apps_last") == apps_array[i][0] &&
					hasNewVer(apps_array[i]) && 
					(sw_mode == 3 || link_internet == "2"))
				htmlcode += '</div><div style="color:#FC0;margin-top:10px;"><span class="app_action" onclick="apps_form(\'upgrade\',\''+ apps_array[i][0] +'\',\'\');"><#update_available#></span>\n';	
			else if(cookie.get("apps_last") == apps_array[i][0])
				htmlcode += "</div><div style=\"color:#FC0;margin-top:10px;margin-left:10px;\"><span class=\"app_no_action\" onclick=\"\"><#liveupdate_up2date#></span>\n";

		}
		else{
			
			if(apps_array[i][0] == "downloadmaster" || apps_array[i][0] == "mediaserver" || apps_array[i][0] == "aicloud" || apps_array[i][0] == "mediaserver2" || apps_array[i][0] == "fileflex")
				htmlcode += '<span class="app_action" onclick="_appname=\''+apps_array[i][0]+'\';check_usb_app_dev();"><#Excute#></span>\n';		/* untranslated */
			else
				htmlcode += '<span class="app_action" onclick="apps_form(\'install\',\''+ apps_array[i][0] +'\',\''+ partitions_array[i] +'\');"><#Excute#></span>\n';		/* untranslated */
		}
		
		htmlcode += '</div><br/><br/></td></tr>\n';
	
		// setCookie
		if(apps_array[i][0] == "downloadmaster"){ // set Cookie
			//set cookie for help.js		
			_dm_install = apps_array[i][3];
			_dm_enable = apps_array[i][4];
		}
	}

	htmlcode += '</table>\n';
	document.getElementById("app_table").innerHTML = htmlcode;
	divdisplayctrl("", "none", "none", "none");
	stoppullstate = 1;
	cookie.set("hwaddr", '<% nvram_get("lan_hwaddr"); %>', 1000);
	cookie.set("apps_last", "", 1000);

	if(re_mode == "1"){
		if($("#upnp_link").length > 0){
			$("#upnp_link").attr({
				"style": "color: #FFCC00; text-decoration:underline;",
				"href": "/mediaserver.asp",
			});
		}

		if($("#ftp_link").length > 0){
			$("#ftp_link").attr({
				"style": "color: #FFCC00; text-decoration:underline;",
				"href": "/Advanced_AiDisk_ftp.asp"
			});
		}

		if($("#samba_link").length > 0){
			$("#samba_link").attr({
				"style": "color: #FFCC00; text-decoration:underline;",
				"href": "/Advanced_AiDisk_samba.asp"
			});
		}
	}
	//re-turn FormTitle height
	if($("#FormTitle > table").height() > $("#FormTitle").height())
		$("#FormTitle").height($("#FormTitle > table").height());
}

/* 
The first four digits only contain the APP version.
Extention number should be appended to the end of APP version.
*/
var hasNewVer = function(arr){
	if(arr[1])
		oldVer = arr[1].split(".");
	else
		return false;

	if(arr[2])
		newVer = arr[2].split(".");
	else
		return false;
	
	for(var i=0; i<4; i++){ 
		if(parseInt(newVer[i]) > parseInt(oldVer[i])){
			return true;
			break;
		}
		else if(parseInt(newVer[i]) == parseInt(oldVer[i]))
			continue;
		else
			return false;
	}

	if(oldVer.length < newVer.length)
		return false;
	else if(oldVer.length > newVer.length)
		return true;		
	else{
		if(oldVer[oldVer.length-1] != newVer[newVer.length-1])
			return true;
		else
			return false;
	}

	return false;
}

var partitions_array = [];
function show_partition(){
 	require(['/require/modules/diskList.js?hash=' + Math.random().toString()], function(diskList){
		var htmlcode = "";
		var mounted_partition = 0;
		partitions_array = [];
		document.getElementById("app_table").style.display = "none";
		htmlcode += '<table align="center" style="margin:auto;border-collapse:collapse;">';

 		var usbDevicesList = diskList.list();

		for(var i=0; i < usbDevicesList.length; i++){
			for(var j=0; j < usbDevicesList[i].partition.length; j++){
				partitions_array.push(usbDevicesList[i].partition[j].mountPoint);
				var accessableSize = simpleNum(usbDevicesList[i].partition[j].size-usbDevicesList[i].partition[j].used);
				var totalSize = simpleNum(usbDevicesList[i].partition[j].size);

				if(usbDevicesList[i].partition[j].status == "unmounted")
					continue;
					
				if(usbDevicesList[i].partition[j].isAppDev){
					if(accessableSize > 1)
						htmlcode += '<tr><td class="app_table_radius_left"><div class="iconUSBdisk" onclick="apps_form(\'install\',\'' + _appname +'\',\'' + usbDevicesList[i].partition[j].mountPoint +'\');"></div></td><td class="app_table_radius_right" style="width:300px;">\n';
					else
						htmlcode += '<tr><td class="app_table_radius_left"><div class="iconUSBdisk_noquota"></div></td><td class="app_table_radius_right" style="width:300px;">\n';

					htmlcode += '<div class="app_desc"><b>'+ usbDevicesList[i].partition[j].partName + ' (active)</b></div>';
				}
				else{
					if(accessableSize > 1)
						htmlcode += '<tr><td class="app_table_radius_left"><div class="iconUSBdisk" onclick="apps_form(\'switch\',\''+_appname+'\',\''+usbDevicesList[i].partition[j].mountPoint+'\');"></div></td><td class="app_table_radius_right" style="width:300px;">\n';
					else
						htmlcode += '<tr><td class="app_table_radius_left"><div class="iconUSBdisk_noquota"></div></td><td class="app_table_radius_right" style="width:300px;">\n';
					htmlcode += '<div class="app_desc"><b>'+ usbDevicesList[i].partition[j].partName + '</b></div>'; 
				}

				if(accessableSize > 1)
					htmlcode += '<div class="app_desc"><#Availablespace#>: <b>'+ accessableSize+" GB" + '</b></div>'; 
				else
					htmlcode += '<div class="app_desc"><#Availablespace#>: <b>'+ accessableSize+" GB <span style=\'color:#FFCC00\'>(Disk quota can not less than 1GB)" + '</span></b></div>'; 

				htmlcode += '<div class="app_desc"><#Totalspace#>: <b>'+ totalSize+" GB" + '</b></div>'; 
				htmlcode += '<div class="app_desc"><b>' + usbDevicesList[i].deviceName + '</b></div>'; 
				htmlcode += '</div><br/><br/></td></tr>\n';
				mounted_partition++;	
			}
		}

		if(mounted_partition == 0){
			if(re_mode == "1")
				htmlcode += '<tr height="360px"><td colspan="2" class="perNode_nohover"><span class="app_name" style="line-height:100%"><#no_usb_found#></span></td></tr>\n';
			else
				htmlcode += '<tr height="360px"><td colspan="2" class="nohover"><span class="app_name" style="line-height:100%"><#no_usb_found#></span></td></tr>\n';
		}

		document.getElementById("partition_div").innerHTML = htmlcode;
		document.getElementById("usbHint").innerHTML = "<#DM_Install_partition#> :";
	});
}

function apps_form(_act, _name, _flag){
	cookie.set("apps_last", _name, 1000);

	document.app_form.apps_action.value = _act;
	document.app_form.apps_name.value = _name;
	document.app_form.apps_flag.value = _flag;
	document.app_form.submit();
}

function divdisplayctrl(flag1, flag2, flag3, flag4){
	document.getElementById("app_table").style.display = flag1;
	document.getElementById("partition_div").style.display = flag2;
	document.getElementById("app_state").style.display = flag3;
	document.getElementById("DMDesc").style.display = flag4;

	if(flag1 != "none"){ // app list
		document.getElementById("return_btn").style.display = "none";
	}
	else if(flag2 != "none"){ // partition list
		setInterval(show_partition, 2000);
		show_partition();
		document.getElementById("return_btn").style.display = "";
	}
	else if(flag4 != "none"){ // help
		var header_info = [<% get_header_info(); %>];
		var host_name = header_info[0].host;
		var _quick_dmlink = "http://" + host_name + ":" + dm_http_port;
		
		if(_dm_enable == "yes"){
			document.getElementById("realLink").href = _quick_dmlink;
		}
		else{
			document.getElementById("quick_dmlink").onclick = function(){alert("<#DM_DisableHint#>");return false;}
		}	
			
		document.getElementById("return_btn").style.display = "";
	}

	if(flag4 == "none")
		document.getElementById("usbHint").style.display = "";
	else
		document.getElementById("usbHint").style.display = "none";
}

function reloadAPP(){
	document.app_form.apps_action.value = "";
	document.app_form.apps_name.value = "";
	document.app_form.apps_flag.value = "";
	location.href = "/APP_Installation.asp";
}

function go_modem_page(usb_unit_flag){
	document.act_form.wan_unit.value = usb_unit_flag;
	document.act_form.action_mode.value = "change_wan_unit";
	document.act_form.target = "";
	document.act_form.submit();
	location.herf = default_apps_array[3][1];
}
function check_usb_app_dev(){
	get_app_dev_info(function(usbAppDevInfo){
		if(usbAppDevInfo.hasAppDev){
			if(usbAppDevInfo.availableSize)
				apps_form("install", _appname, usbAppDevInfo.mountPoint);
			else
				alert("Disk quota can not less than 1GB");
		}
		else {
			location.href = "#";
			divdisplayctrl("none", "", "none", "none");
		}
	});
}
function loginAcc(){
	window.open('https://asus.fileflex.com', '_blank');
}
</script>
</head>

<body onload="initial();" onunload="unload_body();" class="bg">
<div id="TopBanner"></div>
<div id="Loading" class="popup_bg"></div>

<iframe name="hidden_frame" id="hidden_frame" width="0" height="0" frameborder="0" scrolling="no"></iframe>
<form method="post" name="app_form" action="/APP_Installation.asp">
<input type="hidden" name="preferred_lang" value="<% nvram_get("preferred_lang"); %>" disabled>
<input type="hidden" name="firmver" value="<% nvram_get("firmver"); %>" disabled>
<input type="hidden" name="apps_action" value="">
<input type="hidden" name="apps_path" value="">
<input type="hidden" name="apps_name" value="">
<input type="hidden" name="apps_flag" value="">
</form>
<form method="post" name="form" action="/start_apply.htm" target="hidden_frame">
<input type="hidden" name="preferred_lang" id="preferred_lang" value="<% nvram_get("preferred_lang"); %>">
<input type="hidden" name="firmver" value="<% nvram_get("firmver"); %>">
<input type="hidden" name="action_mode" value="">
<input type="hidden" name="action_script" value="">
<input type="hidden" name="action_wait" value="">
</form>

<table id="content_table" align="center" cellspacing="0" style="margin:auto;">
  <tr>
	<td width="17">&nbsp;</td>
	
	<!--=====Beginning of Main Menu=====-->
	<td valign="top" width="202">
	  <div id="mainMenu"></div>
	  <div id="subMenu"></div>
	</td>
	
    <td valign="top">
		<div id="tabMenu" class="submenuBlock"></div>
		<br>
<!--=====Beginning of Main Content=====-->
<div id="FormTitle" style="display:none;">
<table>
  <tr>
	<td>
		<div style="margin-top: 10px;">
			<span class="formfonttitle" style="font-size: 18px;"><#Menu_usb_application#></span>
			<span style="float:right;">
			<img id="return_btn" onclick="reloadAPP();" align="right" style="cursor:pointer;position:absolute;margin-left:-40px;margin-top:-25px; display:none;" title="<#Menu_usb_application#>" src="/images/backprev.png" onMouseOver="this.src='/images/backprevclick.png'" onMouseOut="this.src='/images/backprev.png'">
			</span>
		</div>
	</td>
  </tr> 
  <tr>
	<td><div id="splitLine1" class="splitLine"></div></td>
  </tr>
  <tr>
		<td>
			<div class="formfontdesc" id="usbHint"></div>
		</td>
  </tr>

  <tr>
   	<td valign="top">
		<div id="partition_div"></div>
		<div id="app_state" class="app_state">
			<span style="margin-left:15px;" id="apps_state_desc"><#APP_list_loading#></span>
			<img id="loadingicon" style="margin-left:10px" src="/images/InternetScan.gif">
			<br>
			<br>
			<br>
			<div id="cancelBtn" style="display:none;">
				<input class="button_gen" onclick="apps_form('cancel','','');" type="button" value="<#CTL_Cancel#>"/>
			</div>
		</div>
		<div id="DMDesc" style="display:none;">
			<div style="margin-left:10px;" id="isInstallDesc">
				<h2><#DM_Install_success#></h2>
				<table>	
					<tr>
					<td><div class="top-heading" style="cursor:pointer;margin-top:10px;height:20px;" id="quick_dmlink"><a id="realLink" href="" target="_blank"><b style="text-decoration:underline;color:#FC0;font-size:16px;"><#DM_launch#></b></></a></div></td>
					<td><div style="margin-left:10px;"><img src="images/New_ui/aidisk/steparrow.png"></div></td>
					</tr>		
				</table>
			<br/>
			<div id="splitLine2" class="splitLine"></div>
			<table class="" cellspacing="0" cellpadding="0">
				<tbody>
					<tr valign="top">
					<td>
						<ul style="margin-left:10px;">
							<br>
							<li>
								<a id="faq" href="" target="_blank" style="text-decoration:underline;font-size:14px;font-weight:bolder;color:#FFF"><#DM_title#> FAQ</a>
							</li>
							<li style="margin-top:10px;">
								<a id="faq2" href="" target="_blank" style="text-decoration:underline;font-size:14px;font-weight:bolder;color:#FFF"><#DM_title#> Tool FAQ</a>
							</li>
							<li style="margin-top:10px;">
								<a id="DMUtilityLink" href="http://dlcdnet.asus.com/pub/ASUS/wireless/RT-AC5300/UT_Download_Master_2228_Win.zip" style="text-decoration:underline;font-size:14px;font-weight:bolder;color:#FFF"><#DM_Download_Tool#></a>
							</li>
						</ul>
					</td>
					</tr>
				</tbody>
			</table>													
   	</td> 
  </tr>  
	   
  <tr>
   	<td valign="top" id="app_table_td" height="0px">
				<div id="app_table"></div>
   	</td> 
  </tr>  
</table>
</div>
<!--=====End of Main Content=====-->
		</td>

		<td width="20" align="center" valign="top"></td>
	</tr>
</table>
</div>

<div id="footer"></div>
<form method="post" name="act_form" action="/apply.cgi" target="hidden_frame">
<input type="hidden" name="action_mode" value="">
<input type="hidden" name="action_script" value="">
<input type="hidden" name="wan_unit" value="">
<input type="hidden" name="current_page" value="Advanced_Modem_Content.asp">
</form>
</body>
</html>

