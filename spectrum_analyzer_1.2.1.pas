{$mainresource hwres.res}
{$apptype windows}

program spectrum_analyzer;

uses graphAbc, system.IO.Ports, system;

const
  port_name = 'COM3'; {имя последовательного порта}
  port_speed = 115200; {скорость последовательного порта}
  
  margin_x = 50;
  margin_y = 50;
  
  zero_x = margin_x;
  
  bar_margin_x = 2;
  bar_margin_y = 1;
  
  max_data = 4096; {максимальное кол-во элементов в массиве данных }

var
  screen_width:word := 800; {ширина графического окна}
  screen_height:word := 400; {высота графического окна}
  
  zero_y := screen_height - margin_y; {начальная координа по у}

  length_x := screen_width - 2 * margin_x; {длина оси х}
  length_y := screen_height - 2 * margin_y; {длина оси у}

type  
    measurement_info = record
    max_level:word;
    peak_index:word;
    log_base:word;
    sample_freq:word;
    total_band_count:word;
    data_size:word;
    bar_width:integer;
    peak_bar_index:integer;
    peak_freq:real;
  end;

  input_point = record 
    level:word;
    band_count:word;
  end;
  
  output_point = record
    level:real;
    frequency:real;
  end;

  input_array = array[1..max_data] of input_point;
  output_array = array[1..max_data] of output_point;

procedure resize;
begin  
  screen_width := Window.Width;
  screen_height := Window.Height;
end;

{Рисование координатных осей}
procedure DrawAxis(measurement:measurement_info; var dst_data:output_array);
var
  i:integer;
  text_height:integer;
  x, y:integer;
  lbl:string;
  lbl_ratio:integer;
  
begin  
  text_height := TextHeight('0'); 
  
  OnResize := resize;     
    
  zero_y := screen_height - margin_y;

  length_x := screen_width - 2 * margin_x;
  length_y := screen_height - 2 * margin_y;

  line(zero_x, zero_y, zero_x + length_x, zero_y); 
  line(zero_x, zero_y, zero_x, zero_y - length_y);
  
  SetWindowCaption('Анализатор спектра сигнала (' + IntToStr(screen_width) + 'x' + IntToStr(screen_height) + ')');
  
  SetBrushColor(clWhite);
  
  TextOut(zero_x + length_x + 10, zero_y - text_height div 2, 'f (Гц)');
  TextOut(zero_x, zero_y - length_y - text_height - 10, 'Амплитуда');
  
  if measurement.peak_index <> 0 then
    TextOut(zero_x + length_x div 2, zero_y - length_y - text_height - 10, 'Пик: ' + IntToStr(round(measurement.peak_freq)) + ' Гц');
  
  lbl_ratio := text_height div measurement.bar_width + 1;
  
  for i := 1 to measurement.data_size do
  begin
    x := zero_x + bar_margin_x + (i - 1) * measurement.bar_width + measurement.bar_width div 2;
    y := zero_y;
      
    Coordinate.SetTransform(x, y, 90, 1, 1);  
    line(0, 0, 5, 0);
    
    if (i - 1) mod lbl_ratio = 0 then
    begin
      lbl := IntToStr(round(dst_data[i].frequency));
      TextOut(8, -(text_height div 2), lbl);         
    end;
  end;
         
  Coordinate.SetTransform(0, 0, 0, 1, 1);
end;

{Рисование одной полосы спектра}
procedure DrawSpectrumBar (index, width:integer; point:output_point; c:color);
begin
  SetBrushColor(c);
  FillRect(zero_x + bar_margin_x + (index - 1) * width, zero_y - bar_margin_y, zero_x + bar_margin_x + width * index - 1, zero_y - bar_margin_y - round(length_y * point.level));
end;

{Процедура рисования полос спектра}
procedure DrawSpectrum (measurement:measurement_info; var data:output_array);
var 
  i:integer;
  c:color;
begin 
  SetPenWidth(0);
  
  for i := 1 to measurement.data_size do
  begin
    if i = measurement.peak_bar_index then
      c := clRed
    else
      c := clBlue;
      
    DrawSpectrumBar(i, measurement.bar_width, data[i], c);
  end;
end;

{функция считывания слова из файла}
function ReadWord (var port: SerialPort):word;
var high, low:byte;
begin
  high := port.ReadByte;  
  low := port.ReadByte;
  ReadWord := (high shl 8) or low;
end;

{Процедура чтения байтов до маркера начала пакета (0хffff)}
procedure ReadPacketMarker (var port: SerialPort);
begin
  repeat
    repeat
    until port.ReadByte = $ff;
  until port.ReadByte = $ff;      
end;

{Процедура чтения пакета данных с СОМ порта}
function ReadPacket (var port: SerialPort; var measurement:measurement_info; var data:input_array):boolean;
var 
  i:integer;
  end_marker:word;
begin
  port.DiscardInBuffer;

  ReadPacketMarker(port);
  
  measurement.max_level := ReadWord(port);
  measurement.peak_index := ReadWord(port);
  measurement.log_base := ReadWord(port);
  measurement.sample_freq := ReadWord(port);
  measurement.total_band_count := ReadWord(port);

  measurement.data_size := ReadWord(port);
  
  if measurement.data_size > max_data then
    measurement.data_size := max_data;  

  for i := 1 to measurement.data_size do
  begin
    data[i].band_count := ReadWord(port);
    data[i].level := ReadWord(port);
  end;
    
  end_marker := ReadWord(port);    
  
  ReadPacket := end_marker = $0000;
end;

{Процедура преобразования сигнала с фиксированной точкой в сигнал с плавующей точкой}
procedure PrepareData(var measurement:measurement_info; var src_data:input_array; var dst_data:output_array);
var 
  i: integer;
  bar_index, next_bar_index: integer;
  band_freq: real;
begin
  {TODO обработать случай, когда элементов массива больше, чем доступных точек}
  measurement.bar_width := length_x div measurement.data_size;
  
  band_freq := measurement.sample_freq / measurement.total_band_count;
  
  measurement.peak_freq := measurement.peak_index * band_freq;
  
  bar_index := 1;
  
  for i := 1 to measurement.data_size do
  begin
    dst_data[i].level := src_data[i].level / measurement.max_level;
    
    if dst_data[i].level > 1.0 then
      dst_data[i].level := 1.0;
    
    next_bar_index := bar_index + src_data[i].band_count;
     
    dst_data[i].frequency := band_freq * ((bar_index + next_bar_index - 1) div 2);
    
    if (measurement.peak_index >= bar_index) and (measurement.peak_index < next_bar_index) then
      measurement.peak_bar_index := i;
    
    bar_index := next_bar_index;
  end;
end;

procedure SetupPort(var port: SerialPort);
begin
  port := SerialPort.Create(port_name, port_speed, Parity.None, 8, StopBits.One);
  port.Open;  
end;

{Процедура инициализации}
procedure Setup (var port: SerialPort);
begin
  SetWindowCaption('Анализатор спектра сигнала');
  SetWindowSize(screen_width, screen_height);
  CenterWindow;
  LockDrawing;

  SetupPort(port);
end;


var
  port: SerialPort; {СОМ порт}

  input_data:input_array;
  output_data:output_array;
  measurement:measurement_info;

begin
  Setup(port);

  while true do
  begin
    Window.Clear;
    
    if not ReadPacket(port, measurement, input_data) then 
      continue;
      
    PrepareData(measurement, input_data, output_data);
  
    DrawAxis(measurement, output_data);   
  
    DrawSpectrum(measurement, output_data);    
      
    Redraw;
  end;
  port.Close;
end.