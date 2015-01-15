var g_domain = 'http://127.0.0.1';//'http://chengshuguang.com';
var g_port = '6868';


function init(){
  $(".task").mouseover(over).mouseleave(out);
  
  $(".btn-success").click(endTask);
  $(".btn-danger").click(doDelete);
  $(".btn-warning").click(refresh);
  
  $('input[class="status"]:first').parent('label').addClass('active');
  $('input[class="time_interval"]:first').parent('label').addClass('active');
  $('#send-message').submit(create_new_task);
  checkCookie();
  task_query(0, 0);
}
 
function over() {  
	$(this).children('p').slideUp();
	$(this).children('div').slideDown("slow");
	$(this).children('hr').css("border-color", "#428BCA");
	$(this).addClass('m_over');

}
function out() {  
	$(this).children('p').slideDown();
	$(this).children('hr').css("border-color", "#FF4040");
	$(this).children('div').slideUp("slow");
	$(this).removeClass('m_over');
}
function add(task_obj){
	//var task_time = task_obj.task_time;
	var date = new Date(parseInt(task_obj.task_time+'000', 10));
	Y = date.getFullYear() + '-';
	M = (date.getMonth()+1 < 10 ? '0'+(date.getMonth()+1) : date.getMonth()+1) + '-';
	D = date.getDate() + ' ';
	var $time = $("<h></h>").text(Y+M+D);

	var $status;
	if(task_obj.task_status == 1)
	  $status = $("<span></span>").addClass("label label-danger").text("未完成...");
	else
	  $status = $("<span></span>").addClass("label label-default").text("已完成");

	var $text = $("<p></p>").text(task_obj.task_content);
	var $div_btn = $("<div></div>").addClass("btn-group");
	var $btn1 = $("<button></button>").addClass("btn btn-danger").text("Delete").click(doDelete);
	var $btn2 = $("<button></button>").addClass("btn btn-success").text("Done").click(endTask);


	$div_btn.append($btn1, $btn2);

	var $task = $("<div></div>").data('task_obj', task_obj).addClass("task").mouseleave(out).mouseover(over)
	.append($time, $("<hr/>"), $status, $text, $div_btn).prependTo($('#wrapper_task'));

	//已完成
	if(task_obj.task_status != 1) {
	  $btn2.addClass("disabled");
	  $task.addClass("finish");
	}
}
//op == 1
function doDelete() {
	var task_obj = $(this).parent().parent().data('task_obj');
	var $btn_tmp = $(this);
	jQuery.ajax({
		url: g_domain+':'+g_port+'/task_op',
	    type: 'GET',
	    data: 'task_id=' + task_obj.task_id +'&op=' + 1,
	    dataType: "jsonp",
	    jsonp: "callback",
	    jsonpCallback:"task_op_cb",
	    
	    success: function(message) {
	      if(message.status == 'success') {
	        $btn_tmp.parent().parent().slideUp("slow");
	      } else {
	        alert("server side error!");
	      }
	    },
	    error: function(){
	      alert("undefined err!");
	    }
	  });
}
//op == 0
function endTask() {
	var task_obj = $(this).parent().parent().data('task_obj');
	var $btn_tmp = $(this);
	jQuery.ajax({
		url: g_domain+':'+g_port+'/task_op',
	    type: 'GET',
	    data: 'task_id=' + task_obj.task_id +'&op=' + 0,
	    dataType: "jsonp",
	    jsonp: "callback",
	    jsonpCallback:"task_op_cb",
	    
	    success: function(message) {
	      if(message.status == 'success') {
	        $btn_tmp.parent().siblings('span').removeClass("label label-danger").addClass("label label-default").text("已完成");
	        $btn_tmp.addClass("disabled");
	        $btn_tmp.parent().parent().addClass("finish");
	      } else {
	        alert("server side error!");
	      }
	    },
	    error: function(){
	      alert("undefined err!");
	    }
	  });
}

function refresh(){
	$("#refresh_btn").button('loading');
	  
	var status;
	var time_interval;

	$('input[class="status"]').parent('label').each(function(index, element){
	    if($(this).hasClass('active')) {
	      var idx = 2-index;
	      status = idx;
	    }
	});
	$('input[class="time_interval"]').parent('label').each(function(index, element){
	    if($(this).hasClass('active')) {
	     var idx = 2-index;
	     time_interval = idx;
	    }
	});
	console.log(':::'+status+'--'+time_interval);

	task_query(status, time_interval);

	$("#refresh_btn").button('reset');

}


function create_new_task(){
  var message = jQuery('#send-message input[name=message]').val();
  if( jQuery.trim( message ) === '' ){
    alert('Enter a message!');
    return false;
  }
  message = encodeURIComponent(message) 
  jQuery.ajax({
	url: g_domain+':'+g_port+'/create_new_task',
    type: 'GET',
    data: 'message=' + message,
    dataType: 'jsonp',
    jsonp: "callback",
    jsonpCallback:"create_task_cb",
    success: function( payload ){
      if( payload.status == 'fail' ){
        alert("failed!");
      } else if( payload.status == 'empty-message' ){
        alert('Enter a message!');
      } else{
        console.log("push:"+payload.status);
        jQuery('#send-message input[name=message]').val('');
        // $(".modal-body").text("New task has been created successfully!>>>");
        $("#myModal").modal('hide');
        refresh();
      }
    },
    error: function(){
      alert("undefined err!");
    }
  });
  return false;
}

//status: 0, 1, 2
//time_interval: 0, 1, 2
function task_query(status, time_interval) {
	console.log('inside task_query-->'+status+','+time_interval);
	if(status == undefined || time_interval == undefined)
	return;
	jQuery.ajax({
		url: g_domain+':'+g_port+'/task_query',
	    type: 'GET',
	    data: 'status=' + status +'&time_interval=' + time_interval,
	    dataType: "jsonp",
	    jsonp: "callback",
	    jsonpCallback:"task_query_cb",
	    
	    success: function(message){
	      $('#wrapper_task').empty();
	      $.each(message, function(idx, task) {
	        add(task);
	      });
	    
	    },
	    error: function(){
	      $('#wrapper_task').empty();
	      console.log("pull return error\n");
	     // alert("err");
	    }
	  });
}





//////////////////////////
///cookie
/////////////////////////
function getCookie(c_name)
{
	if (document.cookie.length>0)
	{ 
		c_start=document.cookie.indexOf(c_name + "=")
		if (c_start!=-1)
		{ 
			c_start=c_start + c_name.length+1 
			c_end=document.cookie.indexOf(";",c_start)
			if (c_end==-1) c_end=document.cookie.length
			return unescape(document.cookie.substring(c_start,c_end))
		} 
	}
	return ""
}

function setCookie(c_name,value,expiredays)
{
	var exdate=new Date()
	exdate.setDate(exdate.getDate()+expiredays)
	document.cookie=c_name+ "=" +escape(value)+
	((expiredays==null) ? "" : "; expires="+exdate.toGMTString())
}

function checkCookie()
{
	username=getCookie('username2')
	if (username!=null && username!="")
	{
		alert('Welcome again '+username+'!')
	}
	else 
	{
		username=prompt('Please enter your name:',"")
		if (username!=null && username!="")
		{
			setCookie('username2',username,365)
			setCookie('auth', '1234567890',365)
		}
	}
}