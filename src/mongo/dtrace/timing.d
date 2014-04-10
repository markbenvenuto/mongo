mongodb*:::message_handler_start
{
    printf("%d %d - size %d\n", tid, arg0, arg1);
    self->s = timestamp;
}

mongodb*:::message_handler_done /self->s/
{
    printf("%d\n", tid);
    @["ns"] = quantize(timestamp - self->s);
    self->s = 0;

}

