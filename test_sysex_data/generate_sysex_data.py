data_byte_counts = [
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
  20, 30, 40, 100, 200, 500, 1000
]

for byte_count in data_byte_counts:
  output_filename = 'sysex_test_' + str(byte_count).zfill(4) + '_data_bytes.syx'
  output_file = open(output_filename, 'wb')
  sysex_bytes = [0xf0]
  for i in range(byte_count):
    sysex_bytes.append(i % 128)
  sysex_bytes.append(0xf7)
  output_file.write(bytes(sysex_bytes))
  output_file.close()