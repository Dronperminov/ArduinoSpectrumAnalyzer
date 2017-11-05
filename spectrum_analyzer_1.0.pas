program spectrum_analyzer;

uses graphAbc, {crt,} system.IO.Ports, system;

const
  port_name = 'COM3'; {имя последовательного порта}
  port_speed = 115200; {скорость последовательного порта}

  screen_width = 800; {ширина графического окна}
  screen_height = 400; {высота графического окна}

  margin_x = 50;
  margin_y = 50;
  
  bar_margin_x = 2;
  bar_margin_y = 1;

  zero_x = margin_x; {начальная координа по х}
  zero_y = screen_height - margin_y; {начальная координа по у}

  length_x = screen_width - 2 * margin_x; {длина оси х}
  length_y = screen_height - 2 * margin_y; {длина оси у}
  
  max_data = 4096; {максимальное кол-во элементов в массиве данных }


type
  input_point = record 
    level:word;
    band_count:word;
  end;

  input_array = array[1..max_data] of input_point;
  output_array = array[1..max_data] of real;

  
{Рисование координатных осей}
procedure DrawAxis(band_count:integer; bar_width:integer);
var
  i:integer;
  
begin
  line(zero_x, zero_y, zero_x + length_x, zero_y); 
  line(zero_x, zero_y, zero_x, zero_y - length_y);
  
  for i:= 1 to band_count do
    line(zero_x + bar_margin_x + (i - 1) * bar_width + bar_width div 2, zero_y, zero_x + bar_margin_x + (i - 1) * bar_width + bar_width div 2, zero_y + 5);
end;

{Рисование одной полосы спектра}
procedure DrawSpectrumBar (index, width:integer; value:real);
begin
  FillRect(zero_x + bar_margin_x + (index - 1) * width, zero_y - bar_margin_y, zero_x + bar_margin_x + width * index - 1, zero_y - bar_margin_y - round(length_y * value));
end;

{Процедура рисования полос спектра}
procedure DrawSpectrum (var data:output_array; count:integer);
var column_width, i:integer;
begin
  {TODO обработать случай, когда элементов массива больше, чем доступных точек}
  column_width := length_x div count;
  SetBrushColor(clblack);
  SetPenWidth(0);
  
  for i := 1 to count do
  begin
     DrawSpectrumBar(i, column_width, data[i]);
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
function ReadPacket (var port: SerialPort; var data:input_array; var count:integer; var max_level:word; var log_base:word; var sample_freq:word; var total_band_count:word):boolean;
var 
  i:integer;
  end_marker:word;
begin
  port.DiscardInBuffer;

  ReadPacketMarker(port);
  
  max_level := ReadWord(port);
  log_base := ReadWord(port);
  sample_freq := ReadWord(port);
  total_band_count := ReadWord(port);

  count := ReadWord(port);
  if count > max_data then
    count := max_data;  

  for i:= 1 to count do
  begin
    data[i].band_count := ReadWord(port);
    data[i].level := ReadWord(port);
  end;
    
  end_marker := ReadWord(port);    
  
  ReadPacket := end_marker = $0000;
end;

{Процедура преобразования сигнала с фиксированной точкой в сигнал с плавующей точкой}
procedure PrepareData(var src_data:input_array; var dst_data:output_array; count:integer; max_level:word);
var i:integer;
begin
  for i := 1 to count do
  begin
    dst_data[i] := src_data[i].level / max_level;
    
    if dst_data[i] > 1.0 then
      dst_data[i] := 1.0;
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
  SetWindowSize(screen_width, screen_height);
  CenterWindow;
  LockDrawing;

  SetupPort(port);
end;



var
  port: SerialPort; {СОМ порт}

  input_data:input_array;
  output_data:output_array;
  data_size:integer;
  max_level:word;
  log_base:word;
  sample_freq:word;
  total_band_count:word;

begin
  Setup(port);

  while true do
  begin
    Window.Clear;
    
    if not ReadPacket(port, input_data, data_size, max_level, log_base, sample_freq, total_band_count) then 
      continue;
      
    PrepareData(input_data, output_data, data_size, max_level);
  
    DrawAxis(data_size, length_x div data_size);
  
    DrawSpectrum(output_data, data_size);    
    
    {
    writeln('max_level: ', max_level, '; count: ', data_size, ';');
    for i:= 1 to 5 do
      writeln('input_data[', i, '] = ', input_data[i], '; output_data[', i, '] = ', output_data[i], ';');
    }
      
    Redraw;
  end;
  port.Close;
end.